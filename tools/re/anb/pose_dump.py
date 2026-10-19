#!/usr/bin/env python3
"""RE-2 T5: pose decode + capture cross-validation for flag-clear `.anb` clips.

Decodes a clip's **pose** (the per-joint transforms) at a frame and compares it,
semantically, with a captured reference still from the port rig (C2's captures,
`out/re/capture/`).  The app cannot play decoded clips yet (the converter path is
future work), so the comparison is data-level + visual: a projected stick figure
next to the reference, plus measured features (stance, plantedness, limb
directions) — never an engine render.

What is decoded
---------------
The clip's per-track key arrays (parse2's T1 decode — the flag-clear per-type
formats) are read as **local rotations** of the bones, one track per record of
the sibling `.skb` (track i <-> record i, in file order — the measured
correspondence: the `.anb`'s 29 tracks and `player_a.skb`'s 29 records are the
same part bank, and the L/R pairs are mirror-symmetric in the decoded data).
The root's per-frame position comes from the pre-region series (T1's vector
accessor reading).

The world pose is composed on the converted skeleton (`out/library/.../wander.glb`,
the M0 converter's output): the glb's joint hierarchy, bind locals (from the
inverse bind matrices) and names.  The animated local rotation replaces the
bind local rotation; the bind local translation is kept.

The bridge (pinned)
-------------------
The clip's quats are not in the glb's local frames: measured, the glb bind local
`L_b` and the `.skb` record's second quat `q2_b` satisfy `L_b = i q2 i^-1`
(conjugation by the 180-deg rotation about x), and the decoded clip quats match
`q2_b` (not `L_b`) — the same `conj_i` map applied to every decoded quat brings
the whole skeleton onto the glb bind within idle-plausible residuals (see
`verify`), makes the arms hang at the sides instead of crossing the body, and is
the map that makes the L/R pairs mirror-symmetric.  The map is applied uniformly
(animated and static tracks alike).  **The bridge covers the bone rotations
only** — the root position series is a *position* series already in the glb's
world frame (its y ≈ the pelvis height above the ground, matching the glb's foot
offsets) and is applied raw, never conjugated.  The eight finger statics
(`YUBI_*`, `OYAYUBI_*`) sit 39-99 deg off the bind along the bones' bend axes —
the rig's relaxed-hand default against a straight-fingered T-pose bind — and are
applied as-is (pinned by `verify`).

Assumptions of the capture comparison (stated, not hidden)
---------------------------------------------------------
- **The capture's animation phase is unknown.**  The idle trio (`idle-anchored`
  +5/+10/+15 s) shows Wander in his input-free idle, but which frame of the 76
  frame `player_a_stand` loop the game was showing is not recoverable from the
  stills.  The comparison is therefore phase-free: the loop's invariants (all
  76 frames) are compared with the capture's features, and one frame is shown as
  a representative for the visual pairing.
- **The clip identity is inferred, not proven**: the idle is taken to be
  `player_a_stand.anb` (the stand loop; the game's own opening state has no
  input).  `player_a_stand_*` fidgets exist and could be the live clip at the
  shot times.
- **The camera differs**: the reference is a third-person perspective view from
  behind and above, ours is an orthographic side/back projection of the same
  pose; image measures are projected quantities (foreshortened), so the numeric
  ratios carry a stated tolerance.  The semantic features (limb directions, the
  stagger's sign, uprightness) are the robust part.
- The reference stills are read-only inputs (a machine-local, gitignored tree);
  everything this tool writes goes under the requested output paths.

Modes
  decode  <anb> [--frame N] [--json OUT] [--skb F] [--glb F]
          per-track local quats (raw + bridged), residuals vs the bind, the
          root series value; `--frame` defaults to 0.
  pose    <anb> [--frame N] [--json OUT]
          the composed world transforms (every glb joint: position + quaternion).
  render  <anb> [--frames A,B,..] [--out PNG] [--ref PNG] [--ref-window X0,Y0,X1,Y1]
          the stick-figure strip and/or the reference side-by-side (needs PIL).
  measure <anb> [--ref PNG] [--ref-window ...] [--json OUT]
          the pose features across the loop, and the reference still's features.
  verify  [--json OUT]
          the pinned assertions (exit non-zero on failure).  Stdlib only.

Stdlib only for decode/pose/verify; render/measure need Pillow (dev-only
artifact helpers; the capture tooling in this repo already uses it).
Inputs are read-only.  Python 3, no writes outside the requested paths.
"""

import argparse
import json
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import parse2  # noqa: E402  (the T1 flag-clear reader; imported, never modified)
import probe  # noqa: E402  (the .skb skeleton reader + sibling/glb finders)

GLB_DEFAULT_REL = "out/library/models/shadow/characters/wander/_cmn/wander.glb"

# The pinned bridge: glb-local = conj_i(anb-local), i.e. (x, -y, -z, w).
# Evidence (docs/notes/re2-xval.md §2): glb bind local == i * (skb q2) * i^-1 for
# every .skb record; the decoded clip quats match the skb q2 convention; the
# bridged pose matches the bind within idle-plausible residuals and puts the
# arms at the sides.
BRIDGE = lambda q: (q[0], -q[1], -q[2], q[3])  # noqa: E731

# The eight finger tracks are static; their quats sit 39..99 deg from the T-pose
# bind, along the bones' own bend axes — the rig's relaxed-hand default pose
# (the bind has straight fingers), not a convention error.  Pinned as a band.
FINGER_PREFIX = ("Bone_YUBI_", "Bone_OYAYUBI_")

# verify's pinned bands (measured 2026-10-19; docs/notes/re2-xval.md §4).
CORE_MAX_DEG = 40.0        # legs/spine/head: measured max 34.9 (MOMO_R @f0)
ARM_MAX_DEG = 60.0         # KATA/JOWA/UDE/HAND: measured max 57.4 (JOWA_R @f0)
FINGER_MIN_DEG = 30.0      # the relaxed-hand band: measured 39.5 .. 99.2
FINGER_MAX_DEG = 110.0
DOT_MIN = 0.95             # adjacent-key dot gate (T1 measured 0.98 on this clip)
BIND_TOL = 1e-4            # composed bind vs the glb world matrices
FOOT_Z_RANGE = 0.005       # the planted foot's z range over the loop (measured 0.0007)
FREE_FOOT_Z_RANGE = 0.15   # the free foot's z range (measured 0.125)
GROUND_Y = (0.0, 0.06)     # both feet's world y band (measured 0.02..0.03)
HAND_X = (0.15, 0.35)      # hands at the sides, |x| band @f0 (measured 0.197..0.222)
HAND_Y = (0.50, 0.75)      # hand height band @f0 (measured 0.624..0.627)
HAND_Z = 0.12              # |hand z| band @f0 (measured 0.0165)
STAGGER_MIN = 0.15         # |toe z_L - z_R| (the measured bind/idle stagger ~0.21)
ROOT_Y = (0.73, 0.76)      # the root series y band (measured 0.7429..0.7470)


def die(msg, code=2):
    sys.stderr.write("pose_dump: %s\n" % msg)
    sys.exit(code)


# ------------------------------------------------------------------ math bits


def qnorm(q):
    n = math.sqrt(sum(c * c for c in q))
    return tuple(c / n for c in q) if n else (0.0, 0.0, 0.0, 1.0)


def quat_angle_deg(a, b):
    """Angle between two rotations (|dot|; the quaternion double cover)."""
    a = qnorm(a)
    b = qnorm(b)
    d = abs(sum(x * y for x, y in zip(a, b)))
    return math.degrees(2.0 * math.acos(min(1.0, d)))


def quat_of_matrix(m):
    """Quaternion (x, y, z, w) of a 3x3 rotation (rows, row-vector convention)."""
    t = m[0][0] + m[1][1] + m[2][2]
    if t > 0:
        s = math.sqrt(t + 1.0) * 2.0
        w = 0.25 * s
        x = (m[2][1] - m[1][2]) / s
        y = (m[0][2] - m[2][0]) / s
        z = (m[1][0] - m[0][1]) / s
    elif m[0][0] > m[1][1] and m[0][0] > m[2][2]:
        s = math.sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2.0
        w = (m[2][1] - m[1][2]) / s
        x = 0.25 * s
        y = (m[0][1] + m[1][0]) / s
        z = (m[0][2] + m[2][0]) / s
    elif m[1][1] > m[2][2]:
        s = math.sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2.0
        w = (m[0][2] - m[2][0]) / s
        x = (m[0][1] + m[1][0]) / s
        y = 0.25 * s
        z = (m[1][2] + m[2][1]) / s
    else:
        s = math.sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2.0
        w = (m[1][0] - m[0][1]) / s
        x = (m[0][2] + m[2][0]) / s
        y = (m[1][2] + m[2][1]) / s
        z = 0.25 * s
    return qnorm((x, y, z, w))


def matrix_of_quat(q):
    x, y, z, w = qnorm(q)
    return ((1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)),
            (2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)),
            (2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)))


# ------------------------------------------------------------------ the model


def load_glb(path):
    """The converted skeleton: joint names, hierarchy, bind locals (from IBMs).

    Returns a dict with `names`, `parent` (node index -> parent node index),
    `world_bind` (row-vector 4x4, translation in the 4th row) and `local_bind`
    per joint node index.  `local_bind = world_bind[j] @ inv(world_bind[parent])`
    — the exact relation the converter used (`AquaModelAdapter.cs`).
    """
    data = parse2.read_bytes(path)
    if data[:4] != b"glTF":
        die("%s is not a glb" % path)
    length = struct.unpack_from("<I", data, 8)[0]
    off = 12
    doc = None
    blob = None
    while off + 8 <= length:
        chunk_len, chunk_type = struct.unpack_from("<II", data, off)
        body = data[off + 8:off + 8 + chunk_len]
        if chunk_type == 0x4E4F534A:  # "JSON"
            doc = json.loads(body.decode("utf-8").rstrip("\x00 "))
        elif chunk_type == 0x004E4942:  # "BIN"
            blob = body
        off += 8 + chunk_len
    if doc is None or blob is None or not doc.get("skins"):
        die("%s: no glb skin/binary chunk" % path)
    skin = doc["skins"][0]
    joints = skin["joints"]
    names = [doc["nodes"][j].get("name", "") for j in joints]
    parent = {}
    for j, node in enumerate(doc["nodes"]):
        for child in node.get("children", []):
            parent[child] = j

    acc = doc["accessors"][skin["inverseBindMatrices"]]
    if acc["count"] < len(joints):
        die("%s: %d inverse bind matrices for %d joints" % (path, acc["count"], len(joints)))
    view = doc["bufferViews"][acc["bufferView"]]
    start = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    flat = struct.unpack_from("<%df" % (acc["count"] * 16), blob, start)
    ibm = {}
    for i, j in enumerate(joints):
        m = [list(flat[i * 16 + r * 4:i * 16 + r * 4 + 4]) for r in range(4)]
        ibm[j] = m

    world_bind = {j: invert4(ibm[j]) for j in joints}
    local_bind = {}
    for j in joints:
        p = parent.get(j)
        local_bind[j] = world_bind[j] if p is None else mat4_mul(world_bind[j], invert4(world_bind[p]))
    return {"names": names, "joints": joints, "parent": parent,
            "world_bind": world_bind, "local_bind": local_bind}


def mat4_mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def invert4(m):
    """4x4 inverse (Gauss-Jordan; row-vector layout, translation in row 3)."""
    a = [row[:] + [1.0 if i == j else 0.0 for j in range(4)] for i, row in enumerate(m)]
    for col in range(4):
        piv = max(range(col, 4), key=lambda r: abs(a[r][col]))
        if abs(a[piv][col]) < 1e-12:
            die("singular matrix in the glb inverse bind matrices")
        a[col], a[piv] = a[piv], a[col]
        pv = a[col][col]
        a[col] = [v / pv for v in a[col]]
        for r in range(4):
            if r != col and a[r][col]:
                f = a[r][col]
                a[r] = [v - f * w for v, w in zip(a[r], a[col])]
    return [row[4:] for row in a]


class Clip(object):
    """One flag-clear clip: the T1 decode + the track->bone mapping."""

    def __init__(self, path, glb_path=None, skb_path=None):
        self.path = parse2.resolve(path)
        self.data = parse2.read_bytes(self.path)
        try:
            self.info = parse2.parse_anb(self.data, self.path)
        except ValueError as exc:
            die(str(exc))
        skb = skb_path or probe.find_sibling_skb(self.path)
        if not skb or not os.path.isfile(skb):
            die("no sibling .skb for %s (pass --skb)" % path)
        self.skb_path = skb
        self.records = probe.parse_skb(skb)["records"]
        glb = glb_path or probe.find_glb(None)
        if not glb:
            die("no glb skeleton found (pass --glb; default %s)" % GLB_DEFAULT_REL)
        self.glb_path = glb
        self.glb = load_glb(glb)
        self.name_to_joint = {n: j for j, n in enumerate(self.glb["joints"])
                              for n in [self.glb["names"][j]]}
        self.tracks = []
        for i, row in enumerate(self.info["track_rows"]):
            keys, meta = parse2.decode_track(self.data, row)
            bone = self.records[i]["name"] if i < len(self.records) else None
            self.tracks.append({"index": i, "row": row, "keys": keys, "meta": meta,
                                "bone": bone})
        resolved = sum(1 for t in self.tracks
                       if t["bone"] and t["bone"] in self.name_to_joint)
        if resolved * 2 < len(self.tracks):
            die("%s: only %d/%d of the sibling .skb's bones are in %s — wrong "
                "skeleton (pass --glb for this model)" % (
                    path, resolved, len(self.tracks), os.path.basename(glb)))
        self.root = None
        if self.info["root_keys"] is not None:
            try:
                self.root = parse2.decode_root(self.info)
            except ValueError:
                self.root = None

    # -- decoding -------------------------------------------------------------

    def local_quat(self, track, frame):
        """The track's local rotation at a frame: raw decode + the pinned bridge.

        Returns (raw, bridged, kind): kind is 'keyed', 'static' or 'error'.
        """
        row = track["row"]
        if row["type"] == 0:
            raw = tuple(track["meta"]["static_quaternion"])
            return raw, BRIDGE(raw), "static"
        keys = track["keys"]
        if not keys:
            return None, None, "error"
        raw = tuple(keys[frame % len(keys)])
        return raw, BRIDGE(raw), "keyed"

    def anim_rotations(self, frame):
        """bone name -> bridged local rotation matrix at `frame`."""
        rots = {}
        for track in self.tracks:
            raw, bridged, kind = self.local_quat(track, frame)
            if kind == "error" or not track["bone"]:
                continue
            if track["bone"] in self.name_to_joint:
                rots[track["bone"]] = matrix_of_quat(bridged)
        return rots

    def residuals(self, frame=0):
        """Per-bone angle between the bridged decoded local and the glb bind local."""
        out = []
        for track in self.tracks:
            raw, bridged, kind = self.local_quat(track, frame)
            bone = track["bone"]
            if kind == "error" or not bone or bone not in self.name_to_joint:
                continue
            j = self.name_to_joint[bone]
            local = self.glb["local_bind"][j]
            bind_q = quat_of_matrix([row[:3] for row in local[:3]])
            out.append({"bone": bone, "kind": kind, "residual_deg":
                        round(quat_angle_deg(bridged, bind_q), 2)})
        return out

    # -- composition ----------------------------------------------------------

    def world_pose(self, frame, bridge=True):
        """The composed world matrices per joint node index (row-vector 4x4).

        bind locals (translation kept) with the animated rotation substituted;
        the root's world translation comes from the clip's root series (the
        pre-region position series — T1's vector accessor reading).  `bridge`
        False reproduces the glb bind (used by verify).
        """
        rots = self.anim_rotations(frame) if bridge else {}
        order = []
        seen = set()

        def visit(j):
            if j in seen:
                return
            seen.add(j)
            p = self.glb["parent"].get(j)
            if p is not None:
                visit(p)
            order.append(j)

        for j in self.glb["joints"]:
            visit(j)
        world = {}
        root_shift = None
        if bridge and self.root is not None:
            root_shift = self.root["values"][frame % self.root["keys"]]
        root_node = self.name_to_joint.get("Bone_root")
        for j in order:
            local = [row[:] for row in self.glb["local_bind"][j]]
            name = self.glb["names"][j]
            if name in rots:
                rot = rots[name]
                for r in range(3):
                    local[r][:3] = list(rot[r])
            if root_shift is not None and j == root_node:
                local[3][0], local[3][1], local[3][2] = root_shift
            p = self.glb["parent"].get(j)
            world[j] = local if p is None else mat4_mul(local, world[p])
        return world

    def joint_world(self, world, bone):
        j = self.name_to_joint.get(bone)
        if j is None:
            return None
        return world[j][3][:3]


# ------------------------------------------------------------- shared analysis

CORE_BONES = ("Bone_KOSI", "Bone_MOMO_L", "Bone_SUNE_L", "Bone_TOE_L", "Bone_TUMASAKI_L",
              "Bone_MOMO_R", "Bone_SUNE_R", "Bone_TOE_R", "Bone_TUMASAKI_R",
              "Bone_HARA", "Bone_MUNE", "Bone_KUBI", "Bone_KAO")
ARM_BONES = ("Bone_KATA_L", "Bone_JOWA_L", "Bone_UDE_L", "Bone_HAND_A_L",
             "Bone_KATA_R", "Bone_JOWA_R", "Bone_UDE_R", "Bone_HAND_A_R")


def _hand_features(f0, height):
    """The model side of the hand comparison: the wrist joint (HAND_A) and the
    fist (the centroid of the finger joints), per side, as height/lateral
    fractions — the quantities the reference stills' cuff/fist measures pair with.
    """
    out = {}
    for side, wrist in (("R", "Bone_HAND_A_R"), ("L", "Bone_HAND_A_L")):
        bones = ["Bone_YUBI_A_%s" % side, "Bone_YUBI_B_%s" % side,
                 "Bone_OYAYUBI_A_%s" % side, "Bone_OYAYUBI_B_%s" % side]
        pts = [f0[b] for b in bones if b in f0]
        if not pts or not height:
            continue
        cx = sum(p[0] for p in pts) / len(pts)
        cy = sum(p[1] for p in pts) / len(pts)
        base = min(f0["Bone_TUMASAKI_L"][1], f0["Bone_TUMASAKI_R"][1])
        out[side] = {
            "wrist_height_over_height": round((f0[wrist][1] - base) / height, 4),
            "wrist_lateral_over_height": round(abs(f0[wrist][0]) / height, 4),
            "fist_height_over_height": round((cy - base) / height, 4),
            "fist_lateral_over_height": round(abs(cx) / height, 4),
        }
    return out


def pose_features(clip):
    """The pose features across the whole loop (the phase-free comparison inputs)."""
    frames = clip.info["frames"]
    watch = ("Bone_KOSI", "Bone_KAO", "Bone_TUMASAKI_L", "Bone_TUMASAKI_R",
             "Bone_TOE_L", "Bone_TOE_R",
             "Bone_HAND_A_L", "Bone_HAND_A_R", "Bone_JOWA_L", "Bone_JOWA_R",
             "Bone_SUNE_L", "Bone_SUNE_R",
             "Bone_YUBI_A_L", "Bone_YUBI_B_L", "Bone_OYAYUBI_A_L", "Bone_OYAYUBI_B_L",
             "Bone_YUBI_A_R", "Bone_YUBI_B_R", "Bone_OYAYUBI_A_R", "Bone_OYAYUBI_B_R")
    pos = {b: [] for b in watch}
    for f in range(frames):
        world = clip.world_pose(f)
        for b in watch:
            pos[b].append(clip.joint_world(world, b))

    def rng(b):
        xs = list(zip(*pos[b]))
        return [max(c) - min(c) for c in xs]

    def mean(b):
        n = len(pos[b])
        return [sum(v[c] for v in pos[b]) / n for c in range(3)]

    f0 = {b: pos[b][0] for b in watch}
    stance = [math.sqrt(sum((f0["Bone_TUMASAKI_L"][c] - f0["Bone_TUMASAKI_R"][c]) ** 2
                            for c in range(3)))]
    stance_toe = abs(f0["Bone_TOE_L"][0] - f0["Bone_TOE_R"][0]) \
        if "Bone_TOE_L" in f0 else None
    stance_toe_loop = None
    if "Bone_TOE_L" in f0:
        seps = [abs(pos["Bone_TOE_L"][f][0] - pos["Bone_TOE_R"][f][0])
                for f in range(frames)]
        stance_toe_loop = [round(min(seps), 4), round(max(seps), 4)]
    height = f0["Bone_KAO"][1] - min(f0["Bone_TUMASAKI_L"][1], f0["Bone_TUMASAKI_R"][1])
    return {
        "frames": frames,
        "f0": {b: [round(v, 4) for v in f0[b]] for b in watch},
        "loop_range": {b: [round(v, 4) for v in rng(b)] for b in watch},
        "loop_mean": {b: [round(v, 4) for v in mean(b)] for b in watch},
        "stance_3d_f0": round(stance[0], 4),
        "stance_x_f0": round(abs(f0["Bone_TUMASAKI_L"][0] - f0["Bone_TUMASAKI_R"][0]), 4),
        "stagger_z_f0": round(abs(f0["Bone_TUMASAKI_L"][2] - f0["Bone_TUMASAKI_R"][2]), 4),
        "stagger_sign": ("L_back" if f0["Bone_TUMASAKI_L"][2] < f0["Bone_TUMASAKI_R"][2]
                         else "R_back"),
        "height_f0": round(height, 4),
        "stance_over_height": round(stance[0] / height, 4) if height else None,
        "toe_stance_x_f0": round(stance_toe, 4) if stance_toe is not None else None,
        "toe_stance_over_height": (round(stance_toe / height, 4)
                                   if stance_toe is not None and height else None),
        "toe_stance_x_loop": stance_toe_loop,
        "toe_stance_over_height_loop": (
            [round(v / height, 4) for v in stance_toe_loop] if stance_toe_loop else None),
        "hand_height_over_height": (round(
            (f0["Bone_HAND_A_L"][1] - min(f0["Bone_TUMASAKI_L"][1],
                                          f0["Bone_TUMASAKI_R"][1])) / height, 4)
            if height else None),
        "hand_x_over_height": (round(abs(f0["Bone_HAND_A_L"][0]) / height, 4)
                               if height else None),
        "wrist_and_fist_f0": _hand_features(f0, height),
    }


# ------------------------------------------------------------------ mode: decode


def cmd_decode(args):
    clip = Clip(args.file, glb_path=args.glb, skb_path=args.skb)
    frame = args.frame
    tracks = []
    for track in clip.tracks:
        raw, bridged, kind = clip.local_quat(track, frame)
        entry = {"index": track["index"], "bone": track["bone"],
                 "type": "0x%02X" % track["row"]["type"], "kind": kind,
                 "raw": None if raw is None else [round(v, 6) for v in raw],
                 "bridged": None if bridged is None else [round(v, 6) for v in bridged]}
        tracks.append(entry)
    payload = {
        "file": os.path.relpath(clip.path) if clip.path.startswith(os.path.sep) else clip.path,
        "frames": clip.info["frames"], "tracks_total": clip.info["tracks"],
        "frame": frame,
        "root": None if clip.root is None else {
            "keys": clip.root["keys"],
            "value": [round(v, 6) for v in clip.root["values"][frame % clip.root["keys"]]]},
        "bridge": "(x, -y, -z, w)  [glb-local = conj_i(anb-local)]",
        "residuals_deg": clip.residuals(frame),
        "tracks": tracks,
    }
    if args.json:
        out = os.path.abspath(args.json)
        os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
        with open(out, "w") as fh:
            json.dump(payload, fh, indent=2, sort_keys=True)
            fh.write("\n")
        sys.stderr.write("pose_dump: wrote %s\n" % out)
    print("pose_dump decode %s  frames %d  tracks %d  frame %d" % (
        clip.path, clip.info["frames"], clip.info["tracks"], frame))
    if clip.root is not None:
        print("  root series: %d keys, value @f%d = %s" % (
            clip.root["keys"], frame,
            ["%.4f" % v for v in clip.root["values"][frame % clip.root["keys"]]]))
    for track in clip.tracks:
        raw, bridged, kind = clip.local_quat(track, frame)
        if kind == "error":
            print("  t%02d %-16s 0x%02X  no key data" % (
                track["index"], track["bone"], track["row"]["type"]))
            continue
        print("  t%02d %-16s 0x%02X %-11s bridged (%s)" % (
            track["index"], track["bone"], track["row"]["type"], kind,
            ", ".join("%+.4f" % v for v in bridged)))
    print("  residuals vs glb bind local (deg):")
    for r in payload["residuals_deg"]:
        print("    %-16s %-11s %6.2f" % (r["bone"], r["kind"], r["residual_deg"]))
    return 0


# -------------------------------------------------------------------- mode: pose


def cmd_pose(args):
    clip = Clip(args.file, glb_path=args.glb, skb_path=args.skb)
    frame = args.frame
    world = clip.world_pose(frame)
    joints = []
    for j in clip.glb["joints"]:
        m = world[j]
        joints.append({
            "node": j, "name": clip.glb["names"][j],
            "parent": clip.glb["parent"].get(j),
            "t": [round(v, 6) for v in m[3][:3]],
            "q": [round(v, 6) for v in quat_of_matrix([row[:3] for row in m[:3]])],
        })
    payload = {"file": clip.path, "frame": frame, "frames": clip.info["frames"],
               "glb": clip.glb_path, "skb": clip.skb_path, "joints": joints}
    if args.json:
        out = os.path.abspath(args.json)
        os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
        with open(out, "w") as fh:
            json.dump(payload, fh, indent=2, sort_keys=True)
            fh.write("\n")
        sys.stderr.write("pose_dump: wrote %s\n" % out)
    print("pose_dump pose %s  frame %d  joints %d" % (clip.path, frame, len(joints)))
    for bone in CORE_BONES + ARM_BONES:
        t = clip.joint_world(world, bone)
        if t is not None:
            print("  %-16s %s" % (bone, ["%+.4f" % v for v in t]))
    return 0


# ---------------------------------------------------------------- render (PIL)


def _pil():
    try:
        from PIL import Image, ImageDraw  # noqa: F401
        return Image, ImageDraw
    except ImportError:
        die("render/measure need Pillow (pip install pillow) — decode/pose/verify do not")


def draw_pose(clip, frame, width=520, height=760, view="back", label=True):
    """An orthographic stick figure of the core+arm chains.

    Returns (PIL image, the character's drawn height in px) so a reference still
    can be scaled to the same character height for a fair side-by-side.
    """
    Image, ImageDraw = _pil()
    world = clip.world_pose(frame)
    pts = {}
    for bone in CORE_BONES + ARM_BONES:
        t = clip.joint_world(world, bone)
        if t is not None:
            pts[bone] = t
    ys = [p[1] for p in pts.values()]
    lo, hi = min(ys) - 0.14, max(ys) + 0.10
    scale = (height - 40) / (hi - lo)
    # the back view is drawn as the capture's camera sees it: looking from
    # behind and slightly above, so depth (z, forward) lifts a point in the
    # image — v = y + TILT*z.  The side view keeps the plain z-y plane.
    TILT = 0.25
    if view == "back":
        def proj(p):
            return (-p[0], p[1] + TILT * p[2])
    else:
        def proj(p):
            return (p[2], p[1])
    pys = [proj(p)[1] for p in pts.values()]
    lo, hi = min(pys) - 0.14, max(pys) + 0.10
    scale = (height - 40) / (hi - lo)

    def px(p):
        u, v = proj(p)
        return (width / 2.0 + u * scale, height - 20 - (v - lo) * scale)

    img = Image.new("RGB", (width, height), (14, 18, 22))
    d = ImageDraw.Draw(img)
    for bone in CORE_BONES + ARM_BONES:
        if bone not in pts:
            continue
        # the segment from the nearest drawn ancestor to this bone
        j = clip.name_to_joint[bone]
        p = clip.glb["parent"].get(j)
        while p is not None and clip.glb["names"][p] not in pts:
            p = clip.glb["parent"].get(p)
        if p is None:
            continue
        a = px(pts[clip.glb["names"][p]])
        b = px(pts[bone])
        d.line([a, b], fill=(150, 180, 205), width=3)
    for bone, p in pts.items():
        x, y = px(p)
        d.ellipse([x - 3, y - 3, x + 3, y + 3], fill=(220, 90, 90))
        if label:
            d.text((x + 5, y - 12), bone.replace("Bone_", ""), fill=(170, 190, 200))
    d.text((10, 8), "decoded pose  frame %d/%d  (%s view)" % (
        frame, clip.info["frames"] - 1, view), fill=(220, 220, 220))
    d.text((10, 24), "clip %s  bridge=conj_i  skeleton=glb bind locals" % (
        os.path.basename(clip.path),), fill=(150, 170, 180))
    if view == "back":
        d.text((10, 40), "left on the left (as seen from behind, like the capture)",
               fill=(120, 140, 150))
    # the character's drawn height: head joint to the lowest foot joint
    head = pts.get("Bone_KAO")
    feet = [pts[b][1] for b in ("Bone_TUMASAKI_L", "Bone_TUMASAKI_R") if b in pts]
    drawn = (head[1] - min(feet)) * scale if head and feet else None
    return img, drawn


def crop_reference(path, window):
    Image, _ = _pil()
    img = Image.open(path).convert("RGB")
    if window:
        x0, y0, x1, y1 = window
        img = img.crop((x0, y0, x1, y1))
    return img


def cmd_render(args):
    Image, ImageDraw = _pil()
    clip = Clip(args.file, glb_path=args.glb, skb_path=args.skb)
    frames = [int(v) for v in args.frames.split(",")] if args.frames else [0]
    out = os.path.abspath(args.out)
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    tiles = [draw_pose(clip, f, view=args.view) for f in frames]
    if args.ref:
        ref = crop_reference(args.ref, args.ref_window)
        measured = measure_reference(args.ref, args.ref_window)
        if "bbox_px" in measured:
            # crop to the measured silhouette + a margin, then scale so the
            # character's height matches the drawn pose's character height
            x0, y0, x1, y1 = measured["bbox_px"]
            m = 24
            ref = ref.crop((max(0, x0 - m), max(0, y0 - m),
                            min(ref.width, x1 + m), min(ref.height, y1 + m)))
        target = tiles[0][1]
        if target and measured.get("height_px"):
            factor = target / measured["height_px"]
            ref = ref.resize((max(1, int(ref.width * factor)),
                              max(1, int(ref.height * factor))), Image.LANCZOS)
        pad = 20
        strip = Image.new("RGB", (ref.width + sum(t[0].width for t in tiles) + pad,
                                  max(ref.height, max(t[0].height for t in tiles))),
                          (14, 18, 22))
        strip.paste(ref, (0, 0))
        x = ref.width + pad
        for img, _ in tiles:
            strip.paste(img, (x, 0))
            x += img.width
    else:
        strip = Image.new("RGB", (sum(t[0].width for t in tiles),
                                  max(t[0].height for t in tiles)), (14, 18, 22))
        x = 0
        for img, _ in tiles:
            strip.paste(img, (x, 0))
            x += img.width
    # the phase-free caveat travels with the artifact
    footer = [
        "phase-free comparison: the capture's animation frame is unknown "
        "(the reference still is representative).",
        "the pose shown is frame %s of %d; the loop's invariants are the "
        "comparison, not a frame pairing." % (
            ",".join(str(f) for f in frames), clip.info["frames"] - 1),
    ]
    out_img = Image.new("RGB", (strip.width, strip.height + 14 + 14 * len(footer)),
                        (14, 18, 22))
    out_img.paste(strip, (0, 0))
    d = ImageDraw.Draw(out_img)
    for i, line in enumerate(footer):
        d.text((8, strip.height + 8 + 14 * i), line, fill=(200, 180, 120))
    out_img.save(out)
    print("pose_dump render: %s (%d frames%s)" % (
        out, len(frames), ", ref %s" % args.ref if args.ref else ""))


# --------------------------------------------------------------- measure (PIL)


def measure_reference(path, window):
    """Silhouette features of a reference still: height, feet, stagger, hands.

    The character is separated from the dark world by brightness + low saturation
    (measured on the idle trio: the lit character is 60-120 bright, desaturated;
    the world/moss stays below ~50), then reduced to its largest 4-connected
    component (dropping the subtitle text and speckle).  The feet region is the
    bottom 12% of the silhouette; its pixels split into two x clusters at the
    median, giving the two feet's centers and their lowest rows — the foot nearer
    the camera (the third-person camera sits behind the character) projects lower
    in the image, which is how the stagger's sign is read (a <= 1 px difference
    is reported as a tie).  The hands: each arm's columns are the shoulder band's
    pixels outside +-0.09*height of the body axis; the cuff is the end of the
    contiguous silhouette run walking down from the shoulder band (stopping above
    the boots), and the fist is the dark run below the cuff, reported only when
    it is bounded (a run cut by the cap has merged with the shadow/scabbard); a
    cuff below 0.35*height is the arm's run merging with the body and is
    reported as not measured.
    """
    Image, _ = _pil()
    img = Image.open(path).convert("RGB")
    if window:
        img = img.crop(window)
    px = img.load()
    w, h = img.size
    mask = [[False] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            bright = max(r, g, b)
            if bright > 60 and bright - min(r, g, b) < 25:
                mask[y][x] = True

    # the largest 4-connected component
    seen = [[False] * w for _ in range(h)]
    best = []
    for y0 in range(h):
        for x0 in range(w):
            if not mask[y0][x0] or seen[y0][x0]:
                continue
            stack = [(x0, y0)]
            seen[y0][x0] = True
            comp = []
            while stack:
                x, y = stack.pop()
                comp.append((x, y))
                for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                    if 0 <= nx < w and 0 <= ny < h and mask[ny][nx] and not seen[ny][nx]:
                        seen[ny][nx] = True
                        stack.append((nx, ny))
            if len(comp) > len(best):
                best = comp
    if not best:
        return {"error": "no silhouette found in the window"}
    ys = [y for _, y in best]
    xs = [x for x, _ in best]
    top, bottom = min(ys), max(ys)
    height = bottom - top + 1
    y0 = bottom - max(1, int(0.12 * height))
    feet = [(x, y) for x, y in best if y >= y0]
    fx = sorted(x for x, _ in feet)
    split = fx[len(fx) // 2]
    left = [(x, y) for x, y in feet if x <= split]
    right = [(x, y) for x, y in feet if x > split]

    def center(pxs):
        return (sum(x for x, _ in pxs) / len(pxs),
                sum(y for _, y in pxs) / len(pxs), max(y for _, y in pxs))

    lc = center(left) if left else None
    rc = center(right) if right else None
    outer = None
    if feet:
        outer = max(x for x, _ in feet) - min(x for x, _ in feet)
    # the hands: each arm's columns are the shoulder band's pixels outside
    # +-0.09*height of the body axis; the cuff is the end of the contiguous
    # silhouette run that starts at the shoulder band and walks down (stopping at
    # bottom - 0.25*height, above the boots); the fist is the dark run below the
    # cuff (brightness < 45), capped at 0.10*height and reported only when every
    # column is stopped by a bright pixel (a run cut by the cap has merged with
    # the shadow/scabbard and is not a fist).
    hands = {}
    if lc and rc:
        axis = (lc[0] + rc[0]) / 2.0
        sh_y0 = top + int(0.12 * height)
        sh_y1 = top + int(0.30 * height)
        walk_hi = bottom - int(0.25 * height)
        for side, sel in (("image-left", lambda x: x < axis - 0.09 * height),
                          ("image-right", lambda x: x > axis + 0.09 * height)):
            cols = sorted({x for x, y in best if sh_y0 <= y <= sh_y1 and sel(x)})
            cuffs = []
            for x in cols:
                in_band = [y for y in range(sh_y0, sh_y1 + 1) if mask[y][x]]
                if not in_band:
                    continue
                y = max(in_band)
                while y + 1 <= walk_hi and mask[y + 1][x]:
                    y += 1
                cuffs.append((x, y))
            if not cuffs:
                hands[side] = None
                continue
            cuff_y = max(y for _, y in cuffs)
            cuff_h = (bottom - cuff_y) / height
            if cuff_h < 0.35:
                # a hanging hand's cuff is always above the knee; a lower "run
                # end" is the arm's run merging with the body/hip at this phase
                hands[side] = {
                    "cuff_note": ("no arm run ends above 0.35*height (the arm "
                                  "merges with the body at this phase) — not measured"),
                }
                continue
            cuff_xs = sorted(x for x, y in cuffs if y >= cuff_y - 2)
            entry = {
                "cuff_height_over_height": round(cuff_h, 4),
                "cuff_lateral_over_height": round((sum(cuff_xs) / len(cuff_xs) - axis)
                                                  / height, 4),
            }
            blob = []
            bounded = True
            limit = min(h - 1, cuff_y + int(0.10 * height))
            for x in range(cuff_xs[0], cuff_xs[-1] + 1):
                y = cuff_y + 1
                while y <= limit and not mask[y][x] and max(px[x, y]) < 45:
                    blob.append((x, y))
                    y += 1
                if y > limit:
                    bounded = False
            if blob and bounded:
                cx = sum(x for x, _ in blob) / len(blob)
                cy = sum(y for _, y in blob) / len(blob)
                entry.update({
                    "fist_px": len(blob),
                    "fist_height_over_height": round((bottom - cy) / height, 4),
                    "fist_lateral_over_height": round((cx - axis) / height, 4),
                })
            else:
                entry["fist_note"] = ("the dark run below the cuff is not bounded "
                                      "(shadow/scabbard) — fist not measured")
            hands[side] = entry
    rows_by_y = {}
    for x, y in best:
        rows_by_y.setdefault(y, []).append(x)
    width_at = {}
    for frac in (0.2, 0.35, 0.5, 0.65, 0.8):
        y = top + int(frac * height)
        xs_row = rows_by_y.get(y) or rows_by_y.get(y + 1) or rows_by_y.get(y - 1)
        width_at["%.2f" % frac] = None if not xs_row else round(
            (max(xs_row) - min(xs_row)) / height, 4)
    return {
        "window": list(window) if window else None,
        "filter": "bright > 60, saturation < 25, largest 4-connected component",
        "bbox_px": [min(xs), top, max(xs), bottom],
        "height_px": height,
        "feet_center_sep_px": None if not (lc and rc) else round(rc[0] - lc[0], 1),
        "feet_lowest_px": None if not (lc and rc) else [round(lc[2], 1), round(rc[2], 1)],
        "feet_lower_side": (None if not (lc and rc) else
                            ("tie" if abs(lc[2] - rc[2]) <= 1 else
                             ("image-left" if lc[2] > rc[2] else "image-right"))),
        "sep_over_height": None if not (lc and rc) else round((rc[0] - lc[0]) / height, 4),
        "feet_outer_span_px": outer,
        "outer_over_height": None if outer is None else round(outer / height, 4),
        "width_over_height_at": width_at,
        "hands": hands,
    }


def cmd_measure(args):
    clip = Clip(args.file, glb_path=args.glb, skb_path=args.skb)
    feats = pose_features(clip)
    payload = {"file": clip.path, "pose": feats}
    if args.ref:
        payload["reference"] = measure_reference(args.ref, args.ref_window)
    if args.json:
        out = os.path.abspath(args.json)
        os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
        with open(out, "w") as fh:
            json.dump(payload, fh, indent=2, sort_keys=True)
            fh.write("\n")
        sys.stderr.write("pose_dump: wrote %s\n" % out)
    print("pose_dump measure %s" % clip.path)
    print("  frames %d; f0 stance %.4f (x %.4f) stagger %.4f (%s) height %.4f "
          "stance/height %.3f" % (
              feats["frames"], feats["stance_3d_f0"], feats["stance_x_f0"],
              feats["stagger_z_f0"], feats["stagger_sign"], feats["height_f0"],
              feats["stance_over_height"]))
    for bone in ("Bone_TUMASAKI_L", "Bone_TUMASAKI_R", "Bone_HAND_A_L", "Bone_HAND_A_R",
                 "Bone_KAO", "Bone_KOSI"):
        print("  %-16s f0 %s  loop range %s" % (
            bone, ["%+.4f" % v for v in feats["f0"][bone]],
            ["%.4f" % v for v in feats["loop_range"][bone]]))
    if "reference" in payload:
        r = payload["reference"]
        print("  reference %s:" % args.ref)
        if "error" in r:
            print("    error: %s" % r["error"])
        else:
            print("    height %d px; feet separation %s px (ratio %s); lower foot %s"
                  % (r["height_px"], r["feet_center_sep_px"], r["sep_over_height"],
                     r["feet_lower_side"]))
            for side, h in sorted((r.get("hands") or {}).items()):
                if not h:
                    continue
                if "fist_height_over_height" in h:
                    print("    %s: cuff h %s lat %s; fist h %s lat %s (%s px)"
                          % (side, h["cuff_height_over_height"],
                             h["cuff_lateral_over_height"],
                             h["fist_height_over_height"],
                             h["fist_lateral_over_height"], h.get("fist_px")))
                else:
                    print("    %s: cuff h %s lat %s; %s"
                          % (side, h["cuff_height_over_height"],
                             h["cuff_lateral_over_height"], h.get("fist_note", "")))
    print("  model hand fractions (f0): %s" % json.dumps(feats["wrist_and_fist_f0"]))
    return 0


# ------------------------------------------------------------------ mode: verify


class Check(object):
    def __init__(self, name, expected, measured, ok, detail=""):
        self.name, self.expected, self.measured, self.ok, self.detail = (
            name, expected, measured, ok, detail)


def cmd_verify(args):
    checks = []

    def add(name, expected, measured, ok, detail=""):
        checks.append(Check(name, expected, measured, ok, detail))

    clip = Clip(args.file, glb_path=args.glb, skb_path=args.skb)
    frames = clip.info["frames"]

    # --- the decode (parse2's T1 model on this clip) --------------------------
    keyed = [t for t in clip.tracks if t["row"]["type"] != 0]
    counts_ok = all(t["row"]["keys"] == frames for t in keyed)
    add("every keyed track has frames keys", frames,
        sorted({t["row"]["keys"] for t in keyed}), counts_ok)
    extents_ok = all(t["row"]["extent"] == t["row"]["keys"] * t["row"]["stride"]
                     for t in keyed)
    add("extent == keys x stride on every track", "all", "all" if extents_ok else "NO",
        extents_ok)
    looped = 0
    loop_ok = True
    for t in keyed:
        kp = parse2.BLOB_BASE + t["row"]["keyptr"]
        stride = t["row"]["stride"]
        last = kp + stride * (t["row"]["keys"] - 1)
        same = all(clip.data[kp + j] == clip.data[last + j] for j in range(stride))
        if same:
            looped += 1
        else:
            loop_ok = False
    add("every keyed track loops (first == last key bytes)", "all",
        "%d/%d looped" % (looped, len(keyed)), loop_ok)
    dots = []
    for t in keyed:
        if t["keys"]:
            dots.extend(parse2.adjacent_dots(t["keys"]))
    worst = min(dots) if dots else None
    add("adjacent-key dots >= %.2f" % DOT_MIN, ">= %.2f" % DOT_MIN,
        "min %.4f over %d pairs" % (worst, len(dots)), worst is not None and worst >= DOT_MIN)

    # --- the composition machinery (bind reconstruction) ----------------------
    world_bind = clip.world_pose(0, bridge=False)
    worst_bind = 0.0
    for j in clip.glb["joints"]:
        ref = clip.glb["world_bind"][j]
        got = world_bind[j]
        worst_bind = max(worst_bind, max(abs(ref[r][c] - got[r][c])
                                         for r in range(4) for c in range(4)))
    add("composed bind == glb world matrices", "<= %.0e" % BIND_TOL,
        "max |diff| %.2e" % worst_bind, worst_bind <= BIND_TOL)

    # --- the bridge's cross-file evidence (the .skb q2 vs the glb bind) --------
    worst_q2 = 0.0
    worst_q2_bone = None
    for rec in clip.records:
        bone = rec["name"]
        if bone not in clip.name_to_joint:
            continue
        j = clip.name_to_joint[bone]
        local = clip.glb["local_bind"][j]
        bind_q = quat_of_matrix([row[:3] for row in local[:3]])
        angle = quat_angle_deg(BRIDGE(tuple(rec["quat1"])), bind_q)
        if angle > worst_q2:
            worst_q2, worst_q2_bone = angle, bone
    add("bridge cross-file: conj_i(.skb q2) == glb bind local (every record)",
        "<= 20 deg", "max %.2f (%s)" % (worst_q2, worst_q2_bone), worst_q2 <= 20.0)

    # --- the bridge residuals (frame 0) ---------------------------------------
    res = {r["bone"]: r for r in clip.residuals(0)}
    core = [r for b, r in res.items() if b in CORE_BONES]
    arm = [r for b, r in res.items() if b in ARM_BONES]
    fingers = [r for b, r in res.items() if b.startswith(FINGER_PREFIX)]
    core_worst = max(r["residual_deg"] for r in core)
    add("core chain (legs/spine/head) residual <= %.0f deg" % CORE_MAX_DEG,
        "<= %.0f" % CORE_MAX_DEG, "max %.2f (%s)" % (
            core_worst, max(core, key=lambda r: r["residual_deg"])["bone"]),
        core_worst <= CORE_MAX_DEG)
    arm_worst = max(r["residual_deg"] for r in arm)
    add("arm chain residual <= %.0f deg" % ARM_MAX_DEG, "<= %.0f" % ARM_MAX_DEG,
        "max %.2f (%s)" % (arm_worst, max(arm, key=lambda r: r["residual_deg"])["bone"]),
        arm_worst <= ARM_MAX_DEG)
    fing_ok = all(FINGER_MIN_DEG <= r["residual_deg"] <= FINGER_MAX_DEG for r in fingers)
    add("finger statics in the relaxed-hand band [%d, %d] deg" % (
        FINGER_MIN_DEG, FINGER_MAX_DEG), "all in band",
        "min %.2f max %.2f (%d tracks)" % (
            min(r["residual_deg"] for r in fingers),
            max(r["residual_deg"] for r in fingers), len(fingers)), fing_ok,
        "static finger quats sit off the straight-fingered T-pose bind — the rig's "
        "relaxed-hand default; applied as-is (not an error)")

    # --- the pose semantics across the loop -----------------------------------
    feats = pose_features(clip)
    lf = feats["loop_range"]["Bone_TUMASAKI_L"]
    rf = feats["loop_range"]["Bone_TUMASAKI_R"]
    add("planted foot (L) stays put in z", "<= %.3f" % FOOT_Z_RANGE,
        "%.4f" % lf[2], lf[2] <= FOOT_Z_RANGE)
    add("free foot (R) z range bounded", "<= %.2f" % FREE_FOOT_Z_RANGE,
        "%.4f" % rf[2], rf[2] <= FREE_FOOT_Z_RANGE)
    ys = [feats["loop_mean"]["Bone_TUMASAKI_L"][1], feats["loop_mean"]["Bone_TUMASAKI_R"][1]]
    ys += [feats["f0"]["Bone_TUMASAKI_L"][1], feats["f0"]["Bone_TUMASAKI_R"][1]]
    add("both feet at ground level (y in [%.2f, %.2f])" % GROUND_Y,
        "all in band", "y %s" % ["%.3f" % v for v in ys],
        all(GROUND_Y[0] <= v <= GROUND_Y[1] for v in ys))
    stagger = feats["stagger_z_f0"]
    add("the stance is staggered (|toe z_L - z_R| >= %.2f)" % STAGGER_MIN,
        ">= %.2f" % STAGGER_MIN, "%.4f (%s)" % (stagger, feats["stagger_sign"]),
        stagger >= STAGGER_MIN)
    hand_ok = True
    hand_detail = []
    for bone in ("Bone_HAND_A_L", "Bone_HAND_A_R"):
        x, y, z = feats["f0"][bone]
        ok = HAND_X[0] <= abs(x) <= HAND_X[1] and HAND_Y[0] <= y <= HAND_Y[1] and abs(z) <= HAND_Z
        hand_ok = hand_ok and ok
        hand_detail.append("%s (%+.3f, %+.3f, %+.3f)" % (bone.replace("Bone_", ""), x, y, z))
    add("hands hang at the sides (|x| in %s, y in %s, |z| <= %.2f)" % (
        HAND_X, HAND_Y, HAND_Z), "both", "; ".join(hand_detail), hand_ok)
    head = feats["f0"]["Bone_KAO"]
    hip = feats["f0"]["Bone_KOSI"]
    add("upright: head above hips, near the hip axis", "head.y > hip.y, |dx| <= 0.05",
        "head (%+.3f, %+.3f, %+.3f), hip (%+.3f, %+.3f, %+.3f), dx %.4f" % (
            tuple(head) + tuple(hip) + (head[0] - hip[0],)),
        head[1] > hip[1] and abs(head[0] - hip[0]) <= 0.05)
    root_y = [v[1] for v in [clip.root["values"][f] for f in range(clip.root["keys"])]] \
        if clip.root else []
    add("root series y in [%.2f, %.2f] (pelvis above ground)" % ROOT_Y,
        "all in band", "y [%.4f, %.4f]" % (min(root_y), max(root_y)),
        bool(root_y) and all(ROOT_Y[0] <= v <= ROOT_Y[1] for v in root_y))

    # --- determinism ----------------------------------------------------------
    clip2 = Clip(args.file, glb_path=args.glb, skb_path=args.skb)
    same = all(clip.local_quat(t, 0) == clip2.local_quat(t2, 0)
               for t, t2 in zip(clip.tracks, clip2.tracks))
    add("re-decode is byte-identical", "all tracks equal", "equal" if same else "DIFF", same)

    # --- report ---------------------------------------------------------------
    print("pose_dump verify: %s" % clip.path)
    print("  clip: %d frames, %d tracks (%d keyed), sibling %s" % (
        frames, clip.info["tracks"], len(keyed), os.path.basename(clip.skb_path)))
    for c in checks:
        print("%-5s %-64s expected %s, measured %s%s" % (
            "PASS" if c.ok else "FAIL", c.name, c.expected, c.measured,
            "  [%s]" % c.detail if c.detail and not c.ok else ""))
    failed = [c for c in checks if not c.ok]
    print("summary: %d/%d assertions pass" % (len(checks) - len(failed), len(checks)))
    if args.json:
        out = os.path.abspath(args.json)
        os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
        with open(out, "w") as fh:
            json.dump({"file": clip.path, "checks": [vars(c) for c in checks],
                       "gate": "MET" if not failed else "NOT MET"}, fh, indent=2,
                      sort_keys=True)
            fh.write("\n")
        sys.stderr.write("pose_dump: wrote %s\n" % out)
    return 1 if failed else 0


# ------------------------------------------------------------------------ main


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="pose_dump.py",
        description="RE-2 T5: .anb pose decode + capture cross-validation")
    sub = parser.add_subparsers(dest="command")

    def common(p, with_file=True):
        if with_file:
            p.add_argument("file")
        p.add_argument("--skb", metavar="F", help="the sibling .skb (default: auto)")
        p.add_argument("--glb", metavar="F", help="the skeleton glb (default: %s)"
                       % GLB_DEFAULT_REL)
        p.add_argument("--json", metavar="OUT")

    p = sub.add_parser("decode", help="per-track local quats + residuals")
    common(p)
    p.add_argument("--frame", type=int, default=0)
    p.set_defaults(func=cmd_decode)

    p = sub.add_parser("pose", help="the composed world pose")
    common(p)
    p.add_argument("--frame", type=int, default=0)
    p.set_defaults(func=cmd_pose)

    p = sub.add_parser("render", help="stick-figure strip (+ reference side-by-side)")
    common(p)
    p.add_argument("--frames", help="comma-separated frames (default 0)")
    p.add_argument("--view", choices=("back", "side"), default="back")
    p.add_argument("--out", required=True)
    p.add_argument("--ref", help="reference still (PNG)")
    p.add_argument("--ref-window", metavar="X0,Y0,X1,Y1",
                   help="crop window into the reference (default: the whole image)")
    p.set_defaults(func=cmd_render)

    p = sub.add_parser("measure", help="pose features (+ reference still features)")
    common(p)
    p.add_argument("--ref", help="reference still (PNG)")
    p.add_argument("--ref-window", metavar="X0,Y0,X1,Y1")
    p.set_defaults(func=cmd_measure)

    p = sub.add_parser("verify", help="the pinned assertions (exit non-zero on failure)")
    common(p, with_file=False)
    p.add_argument("--file", default="cp3shadow/anim/player_a_stand.anb")
    p.set_defaults(func=cmd_verify)

    args = parser.parse_args(argv)
    if getattr(args, "ref_window", None):
        args.ref_window = tuple(int(v) for v in args.ref_window.split(","))
    if not args.command:
        parser.print_help()
        return 2
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
