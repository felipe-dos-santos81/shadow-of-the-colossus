#!/usr/bin/env python3
"""S13 Task 16 (ruling R-AH): Phaedra's shake-off strengths, measured on her bake.

  python3 -I tools/re/s13/kirin_shake.py [glb]

The S11/S12 rule (authored, S11 C§6; encounters/quadratus.h and knight.h kShakes): a refuse clip's
stamina drain per second = the p90 of its region bone's angular speed (deg/s, model space, frame to
frame at the clip's own rate) / 10; its active window = the first to the last frame interval above
90 deg/s, in seconds on the clip clock. The glb defaults to $SOTC_LIB/models/shadow/characters/
colossus_04_kirin/_cmn/c04_kirin.glb (her S13 bake, `make bake-kirin`); read-only. It prints one
line per clip; app/src/encounters/kirin.h kShakes carries the numbers (labelled measured).

Which clips (data, graph findings "Shake-off by region"): the base refuse_tail / refuse_body /
refuse_mune and the additive neck/head swings. Which bone each is read on (authored): the bone the
rider's grip region sits on (data, CL: 0x1e0 MATA_L, 0x60/0x80 and 0x160 MUNE, 0x20 KUBI_B, 0x10
HEAD). An additive clip is measured on its own baked pose (its delta's motion: a reading). Stdlib.
"""

import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "s11"))
from quadratus_root import Glb  # noqa: E402  (the glb FK reader)

GLB_REL = "models/shadow/characters/colossus_04_kirin/_cmn/c04_kirin.glb"
CLIPS = [
    ("kirin_a_refuse_tail", "Bone_MATA_L"),
    ("kirin_a_refuse_body", "Bone_MUNE"),
    ("kirin_a_refuse_mune", "Bone_MUNE"),
    ("kirin_a_add_neck_refuse_start", "Bone_KUBI_B"),
    ("kirin_a_add_neck_refuse_loop", "Bone_KUBI_B"),
    ("kirin_a_add_neck_refuse_end", "Bone_KUBI_B"),
    ("kirin_a_add_head_refuse", "Bone_HEAD"),
    ("kirin_a_add_neck_down_2_refuse", "Bone_HEAD"),
]
ACTIVE_DEG_S = 90.0


def angle_deg(a, b):
    d = abs(sum(x * y for x, y in zip(a, b)))
    return math.degrees(2.0 * math.acos(min(1.0, d)))


def shake(speeds, fps):
    """(drain per s, active begin s, active end s) from per-interval speeds (deg/s)."""
    s = sorted(speeds)
    p90 = s[min(len(s) - 1, int(math.ceil(0.9 * len(s))) - 1)] if s else 0.0
    hot = [i for i, v in enumerate(speeds) if v > ACTIVE_DEG_S]
    if not hot:
        return p90 / 10.0, 0.0, 0.0
    return p90 / 10.0, hot[0] / fps, (hot[-1] + 1) / fps


def measure(glb_path):
    glb = Glb(glb_path)
    out = []
    for name, bone in CLIPS:
        clip, frames, last_t = glb.clip(name)
        fps = (frames - 1) / last_t if last_t > 0 else 30.0
        node = glb.index[bone]
        rots = [glb.world(clip, node, f)[1] for f in range(frames)]
        speeds = [angle_deg(rots[i], rots[i + 1]) * fps for i in range(frames - 1)]
        drain, begin, end = shake(speeds, fps)
        out.append((name, bone, frames, fps, drain, begin, end))
    return out


def main(argv):
    lib = os.environ.get("SOTC_LIB", "")
    glb = argv[1] if len(argv) > 1 else os.path.join(lib, GLB_REL)
    if not os.path.isfile(glb):
        raise SystemExit("no glb at %s (set SOTC_LIB to the view with her bake)" % glb)
    for name, bone, frames, fps, drain, begin, end in measure(glb):
        print("%s bone=%s frames=%d fps=%.1f drain=%.1f active=%.2f..%.2f" % (name, bone, frames, fps, drain,
                                                                             begin, end))


# The rule's one check: 10 intervals at 15 fps, three above 90 deg/s (indices 2..4).
assert shake([0, 10, 100, 200, 95, 10, 0, 0, 0, 0], 15.0) == (10.0, 2 / 15.0, 5 / 15.0)  # p90 = 100 (9th of 10)

if __name__ == "__main__":
    main(sys.argv)
