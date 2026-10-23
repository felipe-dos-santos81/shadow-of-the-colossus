#!/usr/bin/env python3
"""R3 (S7): which player_a_home_boss01_cutNN clips carry the wake (lying -> standing).

stdlib only; decodes through tools/re/anb/pose_dump.py (parse2's flag-clear decode + the
pinned bridge) on the converted wander.glb skeleton. These cuts carry **no root position
series** (parse2 root_keys = None, so a bake keeps Bone_root at its bind translation, the
pelvis at the model origin), so the "root height" here is the pelvis (Bone_KOSI) height above
the pose's lowest body joint (pose_dump's CORE_BONES + ARM_BONES — the helper/cloth joints hang
below the body and are skipped): the height the pelvis has with that joint on the floor.
Lying <= 0.4 m, standing >= 0.6 m (the stand loop: 0.67 m with its root series).

Seconds are the baked duration, (frames - 1) / fps with fps = the Q12 rate field x 30
(AnbReader.cs / GlbWriter.cs).

  SOTC_GAME_ROOT=... python3 -I tools/re/s7/wake_clip.py [--glb wander.glb] [--every N]
"""
import argparse, math, os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'anb'))
import pose_dump as pd  # noqa: E402

LYING_M = 0.4
STANDING_M = 0.6
CUTS = ['player_a_home_boss01_cut%02d' % i for i in range(1, 12)]


def fps_of(clip):
    """The Q12 rate field (u16@(structure+6) >> 4, / 4096, 0 -> 1.0) x 30."""
    w = struct.unpack_from('<H', clip.data, clip.info['structure'] + 6)[0] >> 4
    return (w / 4096.0 if w else 1.0) * 30.0


def heights(clip, frame):
    world = clip.world_pose(frame)
    low = min(clip.joint_world(world, b)[1] for b in pd.CORE_BONES + pd.ARM_BONES
              if b in clip.name_to_joint)
    return (clip.joint_world(world, 'Bone_KOSI')[1] - low,
            clip.joint_world(world, 'Bone_KAO')[1] - low)


def heading_deg(clip, frame):
    """Yaw (deg, 0 = +Z, 90 = +X: the app's convention) of the feet: mean ankle->toe-tip on XZ."""
    w = clip.world_pose(frame)
    dx = dz = 0.0
    for side in 'LR':
        ank, tip = clip.joint_world(w, 'Bone_TOE_' + side), clip.joint_world(w, 'Bone_TUMASAKI_' + side)
        dx, dz = dx + tip[0] - ank[0], dz + tip[2] - ank[2]
    return math.degrees(math.atan2(dx, dz))


def analyse(glb=None, every=1):
    """-> (rows, summary). The wake chain starts at the first cut that leaves lying and runs
    until a cut ends standing."""
    rows, wake, wake_s, done, last = [], [], 0.0, False, None
    for name in CUTS:
        c = pd.Clip('cp3shadow/anim/%s.anb' % name, glb_path=glb)
        n, fps = c.info['frames'], fps_of(c)
        hs = [(f,) + heights(c, f) for f in sorted(set(list(range(0, n, every)) + [n - 1]))]
        k0, kn, kmin = hs[0][1], hs[-1][1], min(h[1] for h in hs)
        row = {'clip': name, 'frames': n, 'fps': fps, 'seconds': (n - 1) / fps,
               'keyed': sum(1 for t in c.tracks if t['row']['type'] != 0), 'tracks': len(c.tracks),
               'root_series': c.root is not None, 'pelvis0': k0, 'pelvisN': kn, 'pelvis_min': kmin,
               'head0': hs[0][2], 'headN': hs[-1][2],
               'rise_frame': next((f for f, k, _ in hs if k >= STANDING_M), None),
               'kind': ('rise' if kn > k0 + 0.1 else 'lying' if kn <= LYING_M else
                        'standing' if kmin >= STANDING_M else 'mixed')}
        if (not wake and k0 <= LYING_M and kn > k0 + 0.1) or (wake and not done):
            wake.append(name)
            wake_s += row['seconds']
            done = kn >= STANDING_M
            last = (c, n - 1)
        rows.append(row)
    ref = pd.Clip('cp3shadow/anim/player_a_stand.anb', glb_path=glb)
    summary = {'frames': sum(r['frames'] for r in rows), 'wake': wake, 'seconds': wake_s,
               'complete': done, 'start_pelvis_m': rows[CUTS.index(wake[0])]['pelvis0'] if wake else None,
               'end_pelvis_m': rows[CUTS.index(wake[-1])]['pelvisN'] if wake else None,
               'end_heading_deg': heading_deg(*last) if last else None,
               'stand_heading_deg': heading_deg(ref, 0)}
    return rows, summary


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--glb', help='skeleton glb (default: pose_dump.GLB_DEFAULT_REL)')
    ap.add_argument('--every', type=int, default=1, help='sample every Nth frame')
    a = ap.parse_args()
    rows, s = analyse(a.glb, a.every)
    for r in rows:
        print('%(clip)s frames=%(frames)d fps=%(fps).3f s=%(seconds).3f keyed=%(keyed)d/%(tracks)d '
              'root_series=%(root_series)s pelvis[0]=%(pelvis0).2f pelvis[end]=%(pelvisN).2f '
              'pelvis[min]=%(pelvis_min).2f head[0]=%(head0).2f head[end]=%(headN).2f '
              'rise_frame=%(rise_frame)s %(kind)s' % r)
    print('summary cuts=%d frames=%d wake=%s wake_s=%.3f complete=%s pelvis=%.2f->%.2f '
          'end_heading_deg=%.1f stand_heading_deg=%.1f' % (
              len(rows), s['frames'], ','.join(s['wake']) or '-', s['seconds'], s['complete'],
              s['start_pelvis_m'], s['end_pelvis_m'], s['end_heading_deg'], s['stand_heading_deg']))


if __name__ == '__main__':
    main()
