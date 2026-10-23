#!/usr/bin/env python3
"""S9 gate (W2): compose and verify tools/re/s9/placements.json.

  placements.py <reach-dir> <wander-glb> <probes-bin> [--out tools/re/s9/placements.json]

<reach-dir> holds two dumps of tools/re/s8/reach.cpp `plain` (the app's own heightmap code,
unlayered 1 m map; array 0 = node heights, array 1 = the D3 refuseHoles walk):

  road.bin   reach plain <lib> 301 -7.14 -210 road.bin <PLAIN_GROUND...> <ENTRANCE...>
  arena.bin  reach plain <lib> 353 27.951 345 arena.bin <every encounter.manifest instance>

The road walk is array 1 of road.bin (the Plain stage's rule). The arena stage builds the
same unlayered map without refuseHoles, so every in-bounds move is walkable there: the
arena walk is done here over arena.bin's array 0 (its array 1 is not the arena's rule).

<wander-glb> is a scratch bake of wander.cmsh with the six raise clips (the sword tip).
<probes-bin> is tools/re/s9/probes.cpp built against the app sources: raw-node marches
(heightmap_node / ray_march, step 0.5 m, unit dir) through out/re/stage/road.manifest's ground
and its backdrop ray grid (build_backdrop_grid), combined with fmin (Step 4).
The script re-measures, checks every placement (asserts; exit 1 on a failed check), prints
the evidence lines and writes the JSON. Authored values are constants below, labelled.
Frame: world (the plain/arena frame, identity instances). Yaw: the app's +Y yaw in degrees
(0 faces +Z, 90 faces +X).

Stdlib only; reads the library ($SOTC_LIB), the game tree ($SOTC_GAME_ROOT) read-only.
"""

import importlib.util
import json
import math
import os
import re
import heapq
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
sys.path.insert(0, os.path.join(REPO, "tools", "re", "anb"))
import parse2  # noqa: E402

_spec = importlib.util.spec_from_file_location("s8_placements", os.path.join(REPO, "tools", "re", "s8", "placements.py"))
s8 = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(s8)
Grid, box_dist, check = s8.Grid, s8.box_dist, s8.check

DEFAULT_LIB = "/Users/felipe.dos.santos/Downloads/sotc/sotc-native/out/library"
DEFAULT_GAME = "/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game"
ENCOUNTER = os.path.join(REPO, "out", "re", "stage", "encounter.manifest")
ENTRANCE = ["models/shadow/levels/arena_01_entrance/_cmn/" + n + ".glb" for n in
            ("arena_01_entrance", "arena_01_entrance_furwall", "arena_01_entrance_ground",
             "arena_01_entrance_skirt", "arena_01_entrance_skirt_02", "arena_01_entrance_stairs")]

# ---------------------------------------------------------------- the placements
SPAWN_PLAIN = (301.0, -7.14, -210.0)                 # S8 (data)
ARENA_SPAWN = (353.0, 27.951, 345.0)                 # encounter.manifest `spawn` (playerstartpositions.txt x/z)
ARENA_YAW = 49.24                                    # the manifest's 49.236395, as the brief states it
CORRIDOR = {"x": (300.0, 308.0), "z": (280.0, 296.0)}          # the corridor floor (design §6)
DOOR_CORRIDOR_XZ = ((300.0, 286.0), (308.0, 296.0))  # its dead end (authored box; y measured)
DOOR_ARENA = ((298.0, 27.5, 303.0), (312.0, 32.0, 311.0))      # the bowl's rim over the corridor (authored)
ARRIVE_ROAD = (304.0, 282.0)                         # corridor floor, 4 m outside door_corridor (authored)
ARRIVE_ROAD_YAW = 180.0                              # facing -Z, down the stairs (authored)
OVERHANG = (315.5, 291.0)
VALUS_FEET = (382.0, 28.369, 370.0)
VALUS_CLUB_M = 14.0                                  # app/src/valus.h kValusClubRangeMeters
SUN = (0.4, 0.8, 0.6)
SHADE_M = 300.0                                      # design D6 (authored)
SWORD_GLB = "models/shadow/characters/wander/_cmn/wander_sword.glb"
SWORD_BONE = "OptBone_sword"
CLIPS30 = {"player_a_hoist_sword_start": 30, "player_a_hoist_sword_loop": 50, "player_a_hoist_sword_end": 29,
           "player_a_uma_hoist_sword_start": 35, "player_a_uma_hoist_sword_loop": 60,
           "player_a_uma_hoist_sword_end": 25}
# The sword's particle textures: (role, .cprt, emitter, atlas). The flare is sword_line's own
# `flare_center` glow (labelled: not sword_flare_tip.cprt's flare_02_* atlases); the hit is
# sword_reaction's `flare_radial_ring`, which binds the same glow atlas.
PFX = "particles/shadow/weapon/sword/_ps4/"
TEXTURES = [("lines", "sword_line", "sword_beam_base", "textures/vfx/common/beam/_ps4/vertical_solid_beams_01_pfx"),
            ("overlay", "sword_line", "sword_beam_overlay", "textures/vfx/common/beam/_ps4/vertical_beams_wave_01_pfx"),
            ("flare", "sword_line", "flare_center", "textures/vfx/common/glows/_ps4/soft_glow_01_falloff_pfx"),
            ("hit", "sword_reaction", "flare_radial_ring", "textures/vfx/common/glows/_ps4/soft_glow_01_falloff_pfx")]
FRAME = [1, 1]   # measured: the decoded atlases are single images (512x512, 512x512, 256x256)
# Step 4 — the probes (authored positions; every one checked by raw-node marches below).
ROAD_MANIFEST = os.path.join(REPO, "out", "re", "stage", "road.manifest")
ARRIVE_PLAIN = (301.0, -245.0)                       # S8 door_shrine's plain arrival (the shrine steps' foot)
SUNLIT = (335.0, 233.5)                              # east of the stairs' foot, past the entrance walls' shade
SHADE = (304.0, 284.0)                               # the corridor floor under the cliff, outside door_corridor
STAIRS_FOOT = (304.0, 238.0)                         # the approach's last < 10 deg node before the stairs' ramp
ENTRANCE_TOP = (304.0, 273.0)                        # the corridor floor at the stairs' top
AGRO_STAIRS = (330.0, 228.0)                         # Agro's start for the mounted raise / the e2e lock
TIP_FOOT, TIP_MOUNTED = 2.0, 3.5                     # the raised tip above the ground (authored)
SHADE_STEP = 0.5                                     # ray_march step (ruling B4); hit t is +-(step + 0.5) m
BEAM_M = 150.0
SLOPE_MAX = 10.0
ROUTE_SLOPE = 9.5                                    # the route's search margin under SLOPE_MAX


# ---------------------------------------------------------------- helpers
def floor_box(g, k, lo, hi):
    """(x, y, z) of the grid's non-NaN array-k nodes inside an XZ box."""
    i0, j0 = g.ij(lo[0], lo[1])
    i1, j1 = g.ij(hi[0], hi[1])
    out = []
    for j in range(j0 - 1, j1 + 2):
        for i in range(i0 - 1, i1 + 2):
            x, z, y = g.x0 + i * g.step, g.z0 + j * g.step, g.at(k, i, j)
            if lo[0] <= x <= hi[0] and lo[1] <= z <= hi[1] and not math.isnan(y):
                out.append((x, y, z))
    return out


def in_bounds(g, x, z):
    return g.x0 <= x <= g.x0 + (g.w - 1) * g.step and g.z0 <= z <= g.z0 + (g.h - 1) * g.step


def arena_walk(g, a, lo, hi, step=0.5):
    """Walk the arena's map from `a` toward the box centre: every in-bounds move is walkable
    there (no refuseHoles), feet = the bilinear node height. Returns (metres, entry point,
    min feet, max feet) at the first step inside the 3-D box."""
    c = ((lo[0] + hi[0]) / 2, (lo[2] + hi[2]) / 2)
    d = math.hypot(c[0] - a[0], c[1] - a[2])
    ux, uz = (c[0] - a[0]) / d, (c[1] - a[2]) / d
    ys = []
    for k in range(int(d / step) + 1):
        x, z = a[0] + ux * k * step, a[2] + uz * k * step
        if not in_bounds(g, x, z):
            return None
        y = g.bilinear(0, x, z)
        ys.append(y)
        if lo[0] <= x <= hi[0] and lo[2] <= z <= hi[2] and lo[1] <= y <= hi[1]:
            return k * step, (round(x, 2), round(y, 3), round(z, 2)), min(ys), max(ys)
    return None


# glTF FK with matrix and TRS nodes (the sword bone is a matrix node under the hand).
def _mm(a, b):
    return [[sum(a[r][k] * b[k][c] for k in range(4)) for c in range(4)] for r in range(4)]


def _trs(t, r, s):
    x, y, z, w = r
    m = [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
         [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
         [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]
    return [[m[i][0] * s[0], m[i][1] * s[1], m[i][2] * s[2], t[i]] for i in range(3)] + [[0, 0, 0, 1]]


def node_world(glb, clip, node, frame):
    chain, i = [], node
    while i is not None:
        chain.append(i)
        i = glb.parent.get(i)
    m = [[float(r == c) for c in range(4)] for r in range(4)]
    for i in reversed(chain):
        nd = glb.nodes[i]
        if "matrix" in nd and not any((i, p) in clip for p in ("translation", "rotation", "scale")):
            v = nd["matrix"]
            local = [[v[c * 4 + r] for c in range(4)] for r in range(4)]
        else:
            def at(path, dflt):
                ks = clip.get((i, path))
                return ks[min(frame, len(ks) - 1)] if ks else nd.get(path, dflt)
            local = _trs(at("translation", [0, 0, 0]), at("rotation", [0, 0, 0, 1]), at("scale", [1, 1, 1]))
        m = _mm(m, local)
    return m


def xform(m, p):
    return tuple(sum(m[r][q] * (tuple(p) + (1.0,))[q] for q in range(4)) for r in range(3))


def sword_tip(wander_glb):
    g = s8.root_z.Glb(wander_glb)
    bone = g.index[SWORD_BONE]
    hand = g.parent[bone]
    clip = g.clip("player_a_hoist_sword_loop")
    frames = len(next(iter(clip.values())))
    worst = None
    for f in range(frames):
        mb, mh = node_world(g, clip, bone, f), node_world(g, clip, hand, f)
        hp = xform(mh, (0, 0, 0))
        ends = {z: xform(mb, (0, 0, z)) for z in (-0.58, 0.12)}
        dist = {z: math.dist(ends[z], hp) for z in ends}
        row = (f, dist[-0.58], dist[0.12], ends[-0.58][1] - ends[0.12][1], hp[1])
        if worst is None or row[1] - row[2] < worst[1] - worst[2]:
            worst = row
    return g.nodes[hand]["name"], frames, worst


def marches(probes_bin, lib, queries):
    """Run the harness on (kind, origin[, target, len]) queries; [(ground, backdrop, fmin)]."""
    lines = ["S %f %f %f" % q[1] if q[0] == "S" else "R %f %f %f %f %f %f %f" % (q[1] + q[2] + (q[3],))
             for q in queries]
    r = subprocess.run([probes_bin, lib, ROAD_MANIFEST], input="\n".join(lines) + "\n",
                       capture_output=True, text=True, check=True)
    return [tuple(float(v) for v in ln.split()) for ln in r.stdout.splitlines()]


def yaw_to(a, b):
    return round(math.degrees(math.atan2(b[0] - a[0], b[2] - a[2])), 2)


def leg_worst(g, a, b):
    """The worst slope along a leg past its first 1.5 m (the start's own slope is the arrival's)."""
    d = math.hypot(b[0] - a[0], b[1] - a[1])
    a2 = (a[0] + (b[0] - a[0]) * 1.5 / d, a[1] + (b[1] - a[1]) * 1.5 / d) if d > 1.5 else b
    return s8.walk_line(g, a2, b)[2] if d > 2.5 else g.slope_deg(*b)


def route(g, a, b, smax):
    """Dijkstra over the road walk's reached nodes (8-neighbour, D3 rise/drop, node slope < smax),
    then greedy straight legs that s8.walk_line passes with a worst slope < smax."""
    sa, sb = g.ij(*a), g.ij(*b)
    memo = {}

    def ok(n):
        if n not in memo:
            y = g.at(1, *n)
            memo[n] = not math.isnan(y) and g.slope_deg(g.x0 + n[0] * g.step, g.z0 + n[1] * g.step) < smax
        return memo[n]
    dist, prev, q = {sa: 0.0}, {}, [(0.0, sa)]
    while q:
        d, n = heapq.heappop(q)
        if n == sb:
            break
        if d > dist[n]:
            continue
        y0 = g.at(0, *n)
        for di in (-1, 0, 1):
            for dj in (-1, 0, 1):
                m = (n[0] + di, n[1] + dj)
                if m == n or not ok(m):
                    continue
                y1 = g.at(0, *m)
                if y1 - y0 > 1.0 or y0 - y1 > 1.5:
                    continue
                nd = d + math.hypot(di, dj)
                if nd < dist.get(m, 1e30):
                    dist[m], prev[m] = nd, n
                    heapq.heappush(q, (nd, m))
    path = [sb]
    while path[-1] != sa:
        path.append(prev[path[-1]])
    pts = [(g.x0 + i * g.step, g.z0 + j * g.step) for i, j in reversed(path)]
    pts[0], pts[-1] = a, b
    out, k = [a], 0
    while k < len(pts) - 1:
        m = len(pts) - 1
        while m > k + 1:
            w = s8.walk_line(g, pts[k], pts[m])
            if w[1] == "ok" and leg_worst(g, pts[k], pts[m]) < smax:
                break
            m -= 1
        out.append(pts[m])
        k = m
    return dist[sb], [(round(x, 1), round(z, 1)) for x, z in out[:-1]] + [b]


def glb_json(path):
    d = open(path, "rb").read()
    return json.loads(d[20:20 + struct.unpack_from("<I", d, 12)[0]])


def cprt_refs(path):
    """[(emitter, atlas)] in file order: each `<file>:<emitter>` name and the .cptx after it."""
    s = open(path, "rb").read().decode("latin-1")
    out = []
    for m in re.finditer(r"[a-z_]+:([a-z_]+)\x00", s):
        t = re.search(r"\$/(textures/[\x21-\x7e]+?)\.cptx", s[m.end():])
        out.append((m.group(1), t.group(1) if t else None))
    return out


# ---------------------------------------------------------------- compose
def main(argv):
    args = list(argv)
    out = os.path.join(HERE, "placements.json")
    if "--out" in args:
        k = args.index("--out")
        out = args[k + 1]
        del args[k:k + 2]
    if len(args) != 3:
        sys.stderr.write(__doc__)
        return 2
    rdir, wander_glb, probes_bin = args
    lib = os.environ.get("SOTC_LIB") or DEFAULT_LIB
    game = os.environ.get("SOTC_GAME_ROOT") or DEFAULT_GAME
    road = Grid(os.path.join(rdir, "road.bin"), 2)
    arena = Grid(os.path.join(rdir, "arena.bin"), 2)

    # Step 1 — the road's reach (the Plain stage's map: plain set + the six entrance glbs).
    nodes = road.count(1)
    ent = [(x, y, z) for (x, y, z) in floor_box(road, 1, (280.0, 250.0), (330.0, 320.0))]
    zmax = max(p[2] for p in ent)
    cor = floor_box(road, 1, (CORRIDOR["x"][0], CORRIDOR["z"][0]), (CORRIDOR["x"][1], CORRIDOR["z"][1]))
    cy = (min(p[1] for p in cor), max(p[1] for p in cor))
    check(nodes > 500000, "road: %d reached nodes from spawn_plain %s" % (nodes, SPAWN_PLAIN))
    check(296.0 <= zmax < 297.0, "road: the entrance (x 280..330) reached up to z %.2f" % zmax)
    check(len(cor) > 0 and 2.6 <= cy[0] and cy[1] <= 6.0,
          "road: corridor floor x %s z %s: %d reached nodes, y %.2f..%.2f" % (CORRIDOR["x"], CORRIDOR["z"], len(cor), cy[0], cy[1]))
    ov_h, ov_r = road.node(0, *OVERHANG), road.node(1, *OVERHANG)
    check(math.isnan(ov_r), "road: overhang %s node height %.2f, not reached" % (OVERHANG, ov_h))

    # door_corridor: the corridor's dead end, y spanning the reached floor + 2.5 m.
    (dx0, dz0), (dx1, dz1) = DOOR_CORRIDOR_XZ
    dpts = floor_box(road, 1, (dx0, dz0), (dx1, dz1))
    fy = (min(p[1] for p in dpts), max(p[1] for p in dpts))
    door_c = ((dx0, math.floor((fy[0] - 0.5) * 10) / 10, dz0), (dx1, math.ceil((fy[1] + 2.5) * 10) / 10, dz1))
    check(len(dpts) >= 40, "door_corridor %s..%s: %d reached nodes inside, floor %.2f..%.2f, the walk's z max %.2f"
          % (door_c[0], door_c[1], len(dpts), fy[0], fy[1], zmax))
    check(box_dist(SPAWN_PLAIN, *door_c) > 400, "door_corridor: %.0f m from spawn_plain" % box_dist(SPAWN_PLAIN, *door_c))

    # door_arena: the arena's own map, from its spawn toward the rim above the corridor.
    lo, hi = DOOR_ARENA
    ay = arena.bilinear(0, ARENA_SPAWN[0], ARENA_SPAWN[2])
    check(abs(ay - ARENA_SPAWN[1]) < 0.05, "arena: spawn %s ground %.3f (manifest %.3f)" % (ARENA_SPAWN, ay, ARENA_SPAWN[1]))
    w = arena_walk(arena, ARENA_SPAWN, lo, hi)
    check(w is not None, "arena walk spawn -> door_arena: %.1f m in bounds, enters at %s, feet %.2f..%.2f"
          % (w[0], w[1], w[2], w[3]) if w else "arena walk spawn -> door_arena: never enters")
    apts = [p for p in floor_box(arena, 0, (lo[0], lo[2]), (hi[0], hi[2])) if lo[1] <= p[1] <= hi[1]]
    rim = [p for p in floor_box(arena, 0, (lo[0], lo[2]), (hi[0], hi[2])) if p[1] < lo[1]]
    check(len(apts) >= 60, "door_arena %s..%s: %d bowl nodes inside (y %.2f..%.2f); %d nodes of the drop to the corridor "
          "(y %.1f..%.1f) inside too" % (lo, hi, len(apts), min(p[1] for p in apts), max(p[1] for p in apts), len(rim),
                                         min(p[1] for p in rim), max(p[1] for p in rim)))
    ds, dv = box_dist(ARENA_SPAWN, lo, hi), box_dist(VALUS_FEET, lo, hi)
    check(ds >= 10.0, "door_arena: %.1f m from the arena spawn (>= 10)" % ds)
    check(dv >= 2 * VALUS_CLUB_M, "door_arena: %.1f m from Valus's feet (>= 2 x club range %.0f m; he never translates)"
          % (dv, VALUS_CLUB_M))
    ry = road.bilinear(0, *ARRIVE_ROAD)
    arrive_road = (ARRIVE_ROAD[0], round(ry, 3), ARRIVE_ROAD[1])
    ra = box_dist(arrive_road, *door_c)
    check(not math.isnan(road.node(1, *ARRIVE_ROAD)) and ra >= 3.0,
          "arrive_road %s yaw %.0f: reached corridor floor, %.2f m outside door_corridor (>= 3), slope %.2f deg"
          % (arrive_road, ARRIVE_ROAD_YAW, ra, road.slope_deg(*ARRIVE_ROAD)))
    check(box_dist(ARENA_SPAWN, lo, hi) > 0, "arrive_arena (the arena spawn) is outside door_arena")

    # Step 2 — Valus and the sun.
    ent_line = next(ln.split() for ln in open(ENCOUNTER) if ln.startswith("entity "))
    feet = tuple(float(v) for v in ent_line[2:5])
    check(feet == VALUS_FEET, "valus: encounter.manifest entity %s at %s" % (os.path.basename(ent_line[1]), feet))
    mune = float(re.search(r"`Bone_MUNE` \| \*\*([0-9.]+)\*\*", open(os.path.join(REPO, "docs/notes/s4-results.md")).read()).group(1))
    check(round(mune, 2) == 10.93, "valus: chest lift Bone_MUNE %.4f m (s4-results.md) -> 10.93" % mune)
    metal = open(os.path.join(REPO, "app/shaders/mesh.metal")).read().splitlines()
    sun_line = [k + 1 for k, ln in enumerate(metal) if "normalize(float3(0.4, 0.8, 0.6))" in ln]
    n = math.sqrt(sum(v * v for v in SUN))
    sun_n = [round(v / n, 6) for v in SUN]
    check(len(sun_line) == 1, "sun: mesh.metal:%s normalize(float3(0.4, 0.8, 0.6)) = %s, elevation %.2f deg"
          % (sun_line, sun_n, math.degrees(math.asin(SUN[1] / n))))

    # Step 3 — the sword, its textures, the clips.
    have_sword = os.path.exists(os.path.join(lib, SWORD_GLB))
    sj = glb_json(os.path.join(lib, SWORD_GLB)) if have_sword else None
    acc = sj["accessors"][sj["meshes"][0]["primitives"][0]["attributes"]["POSITION"]] if sj else None
    check(have_sword, "sword: %s in the library, blade local z %.3f..%.3f" % (SWORD_GLB, acc["min"][2], acc["max"][2])
          if have_sword else "sword: missing — dotnet exec out/bin/bp-convert.dll convert --root \"$SOTC_GAME_ROOT\" "
          "--out \"$SOTC_LIB\" --under models/shadow/characters/wander/_cmn/wander_sword.cmsh")
    wj = glb_json(os.path.join(lib, "models/shadow/characters/wander/_cmn/wander.glb"))
    bi = [nd.get("name") for nd in wj["nodes"]].index(SWORD_BONE)
    check(any(bi in s["joints"] for s in wj["skins"]), "sword: %s is joint node %d of Wander's skin (library wander.glb)" % (SWORD_BONE, bi))
    hand, frames, (f, d_tip, d_other, dy, hy) = sword_tip(wander_glb)
    check(d_tip > d_other, "sword: at player_a_hoist_sword_loop (%d frames; closest frame %d) local z -0.58 is %.3f m from %s, "
          "z +0.12 is %.3f m; the -0.58 end is %.3f m above the other" % (frames, f, d_tip, hand, d_other, dy))
    srcs = {}
    for role, cprt, emitter, atlas in TEXTURES:
        refs = dict(cprt_refs(os.path.join(game, PFX + cprt + ".cprt")))
        check(refs.get(emitter) == atlas, "texture %-7s %s.cprt:%s -> %s.cptx" % (role, cprt, emitter, atlas))
        srcs[role] = atlas + ".cptx"
    clip_measured = {}
    for name, f30 in CLIPS30.items():
        data = open(os.path.join(game, "cp3shadow/anim", name + ".anb"), "rb").read()
        info = parse2.parse_anb(data, name)
        rate = parse2.u16(data, parse2.BLOB_BASE + info["structure"] + 6) >> 4
        fps = 30.0 * (1.0 if rate == 0 else rate / 4096.0)
        dur = (info["frames"] - 1) / fps
        check(round(dur * 30) == f30, "clip %-32s %2d keys @ %.4f fps = %.4f s = %d frames @ 30" % (name, info["frames"], fps, dur, f30))
        clip_measured[name] = {"keys": info["frames"], "fps": round(fps, 4), "seconds": round(dur, 4)}

    # Step 4 — the probes: raw-node marches through the road's ground and the backdrop ray grid.
    chest = (VALUS_FEET[0], VALUS_FEET[1] + 10.93, VALUS_FEET[2])
    sun_d = tuple(v / n for v in SUN)

    def at(xz, lift=0.0):
        return (xz[0], road.bilinear(0, *xz) + lift, xz[1])

    def ring(xz):
        return [(xz[0] + a, xz[1] + b) for a in (-1, 0, 1) for b in (-1, 0, 1)]
    for name, xz in (("sunlit", SUNLIT), ("shade", SHADE), ("stairs_foot", STAIRS_FOOT),
                     ("entrance_top", ENTRANCE_TOP), ("agro_stairs", AGRO_STAIRS)):
        check(not math.isnan(road.node(1, *xz)), "probe %-12s %s: reached by the road walk, ground %.3f, slope %.2f deg"
              % (name, xz, road.bilinear(0, *xz), road.slope_deg(*xz)))
    qs = []
    for xz, lift in [(p, TIP_FOOT) for p in ring(SUNLIT)] + [(p, TIP_FOOT) for p in ring(SHADE)] + \
                    [(p, h) for h in (TIP_FOOT, TIP_MOUNTED) for p in ring(AGRO_STAIRS)]:
        o = at(xz, lift)
        qs += [("S", o), ("R", o, chest, BEAM_M)]
    res = marches(probes_bin, lib, qs)
    sh = [res[k] for k in range(0, len(res), 2)]
    bm = [res[k] for k in range(1, len(res), 2)]
    tol = SHADE_STEP + 0.5
    f = lambda v: "none" if math.isnan(v) else "%.1f" % v  # noqa: E731
    beam_c = bm[4]
    check(all(math.isnan(t[2]) for t in sh[0:9]) and all(not math.isnan(t[2]) and t[2] <= BEAM_M for t in bm[0:9]),
          "sunlit %s tip +%.0f m: shade march none within %.0f m (all 9 nodes of the 1 m ring); beam to the chest hits "
          "ground %s / backdrop %s -> %s m (+-%.1f; ring %s..%s)" % (SUNLIT, TIP_FOOT, SHADE_M, f(beam_c[0]), f(beam_c[1]),
          f(beam_c[2]), tol, f(min(t[2] for t in bm[0:9])), f(max(t[2] for t in bm[0:9]))))
    check(all(not math.isnan(t[2]) for t in sh[9:18]),
          "shade %s tip +%.0f m: shade march blocked at ground %s / backdrop %s -> %s m (ring %s..%s)"
          % (SHADE, TIP_FOOT, f(sh[13][0]), f(sh[13][1]), f(sh[13][2]), f(min(t[2] for t in sh[9:18])),
             f(max(t[2] for t in sh[9:18]))))
    check(box_dist(at(SHADE), *door_c) > 0, "shade: outside door_corridor (%.2f m)" % box_dist(at(SHADE), *door_c))
    for k0, lift in ((18, TIP_FOOT), (27, TIP_MOUNTED)):
        c = bm[k0 + 4]
        check(all(math.isnan(t[2]) for t in sh[k0:k0 + 9]) and all(not math.isnan(t[2]) for t in bm[k0:k0 + 9])
              and road.slope_deg(*AGRO_STAIRS) <= SLOPE_MAX,
              "agro_stairs %s tip +%.1f m: sunlit (ring), slope %.2f deg; beam hits ground %s / backdrop %s -> %s m"
              % (AGRO_STAIRS, lift, road.slope_deg(*AGRO_STAIRS), f(c[0]), f(c[1]), f(c[2])))
    check(box_dist(at(ENTRANCE_TOP), *door_c) > 0 and CORRIDOR["x"][0] <= ENTRANCE_TOP[0] <= CORRIDOR["x"][1],
          "entrance_top %s: on the corridor floor (y %.3f), %.2f m short of door_corridor"
          % (ENTRANCE_TOP, road.bilinear(0, *ENTRANCE_TOP), box_dist(at(ENTRANCE_TOP), *door_c)))
    for a, b, what in ((STAIRS_FOOT, ENTRANCE_TOP, "stairs_foot -> entrance_top"),
                       (ENTRANCE_TOP, (304.0, 290.0), "entrance_top -> door_corridor"),
                       (AGRO_STAIRS, STAIRS_FOOT, "agro_stairs -> stairs_foot")):
        m_, why, worst = s8.walk_line(road, a, b)
        check(why == "ok", "%s: %.1f m straight, %s (worst slope %.1f deg)" % (what, m_, why, worst))
    rlen, wps = route(road, ARRIVE_PLAIN, STAIRS_FOOT, ROUTE_SLOPE)
    legs = [s8.walk_line(road, a, b) for a, b in zip(wps, wps[1:])]
    worst = [leg_worst(road, a, b) for a, b in zip(wps, wps[1:])]
    check(all(w[1] == "ok" for w in legs) and max(worst) < SLOPE_MAX,
          "e2e_route %d waypoints, %.0f m (the search's %.0f m): legs %s m; worst slopes past each leg's first 1.5 m %s "
          "(the arrival itself, the shrine steps' foot, is %.2f deg)" % (len(wps), sum(w[0] for w in legs), rlen,
          ["%.0f" % w[0] for w in legs], ["%.2f" % w for w in worst], road.slope_deg(*ARRIVE_PLAIN)))
    probes = {
        "sunlit": {"p": [round(v, 3) for v in at(SUNLIT)], "yaw_away_deg": round((yaw_to(at(SUNLIT), chest) + 360) % 360 - 180, 2),
                   "shade_hit_m": None, "beam_hit_m": beam_c[2]},
        "shade": {"p": [round(v, 3) for v in at(SHADE)], "shade_hit_m": sh[13][2]},
        "stairs_foot": [round(v, 3) for v in at(STAIRS_FOOT)],
        "entrance_top": [round(v, 3) for v in at(ENTRANCE_TOP)],
        "agro_stairs": {"p": [round(v, 3) for v in at(AGRO_STAIRS)], "yaw_deg": yaw_to(at(AGRO_STAIRS), chest),
                        "beam_hit_m": {"tip_2": bm[22][2], "tip_3_5": bm[31][2]}},
        "e2e_route": [list(w) for w in wps],
        "source": "authored|measured",
        "sources": {"sunlit": "authored p (y = ground), yaw_away measured toward Valus's chest + 180; marches measured",
                    "shade": "authored p (y = ground); march measured", "stairs_foot": "authored (y = ground)",
                    "entrance_top": "authored (y = ground)",
                    "agro_stairs": "authored p (y = ground), yaw measured toward Valus's chest; marches measured",
                    "e2e_route": "measured (Dijkstra over the road walk, slope < 9.5 deg, then straight legs)",
                    "marches": "tools/re/s9/probes.cpp: ray_march step 0.5 m, unit dir, ground fmin backdrop; t +-1.0 m"},
        "tip_lift_m": {"foot": TIP_FOOT, "mounted": TIP_MOUNTED},
    }

    doc = {
        "door_corridor": {"stage": "plain", "min": list(door_c[0]), "max": list(door_c[1]),
                          "arrive_arena": {"p": list(ARENA_SPAWN), "yaw_deg": ARENA_YAW}, "source": "authored|data",
                          "box_source": "authored (xz); y = the reached floor -0.5 .. +2.5 m",
                          "arrive_source": "data (the arena spawn; manifest yaw 49.236395)",
                          "reach": {"nodes_inside": len(dpts), "floor_y": [round(fy[0], 3), round(fy[1], 3)]}},
        "door_arena": {"stage": "arena", "min": list(lo), "max": list(hi),
                       "arrive_road": {"p": list(arrive_road), "yaw_deg": ARRIVE_ROAD_YAW}, "source": "authored",
                       "reach": {"walk": "arena unlayered 1 m map, no refuseHoles, straight from the spawn",
                                 "walk_m": w[0], "enters_at": list(w[1]), "bowl_nodes_inside": len(apts),
                                 "spawn_dist_m": round(ds, 2), "valus_dist_m": round(dv, 2),
                                 "arrive_road_outside_door_corridor_m": round(ra, 2)}},
        "valus": {"feet": list(VALUS_FEET), "chest_lift": 10.93, "source": "data",
                  "chest": [VALUS_FEET[0], round(VALUS_FEET[1] + 10.93, 3), VALUS_FEET[2]]},
        "sun": {"dir": list(SUN), "normalized": sun_n, "shade_m": SHADE_M, "source": "authored",
                "shader": "app/shaders/mesh.metal:%d" % sun_line[0]},
        "sword": {"glb": SWORD_GLB, "bone": SWORD_BONE, "tip": [0, 0, -0.58], "source": "measured|authored",
                  "hand_bone": hand, "blade_z": [round(acc["min"][2], 4), round(acc["max"][2], 4)],
                  "note": "the end farther from the hand at player_a_hoist_sword_loop (measured); the tip point on the blade's end (authored)"},
        "textures": {"lines": srcs["lines"][:-5] + ".png", "overlay": srcs["overlay"][:-5] + ".png",
                     "flare": srcs["flare"][:-5] + ".png", "hit": srcs["hit"][:-5] + ".png",
                     "sources": srcs, "frame": FRAME,
                     "emitters": {r: "%s.cprt:%s" % (c, e) for r, c, e, _ in TEXTURES},
                     "note": "flare = sword_line's flare_center glow (labelled; not sword_flare_tip's flare_02_*); "
                             "hit = sword_reaction's atlas, the same glow"},
        "clips": dict(CLIPS30, fps=30),
        "clips_measured": clip_measured,
        "probes": probes,
        "road_reach": {"nodes": nodes, "z_max": round(zmax, 2),
                       "corridor": {"x": list(CORRIDOR["x"]), "z": list(CORRIDOR["z"]), "y": [round(cy[0], 2), round(cy[1], 2)]},
                       "ground": "plain.manifest's 7 instance glbs + " + ", ".join(os.path.basename(e)[:-4] for e in ENTRANCE),
                       "overhang": {"xz": list(OVERHANG), "node_y": round(ov_h, 2), "reached": False}},
    }
    with open(out, "w") as fh:
        json.dump(doc, fh, indent=1)
        fh.write("\n")
    print("wrote %s" % out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
