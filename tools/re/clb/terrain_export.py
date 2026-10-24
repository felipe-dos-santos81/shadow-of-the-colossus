"""Export an arena's stage terrain classes and owners to `<arena>.terrain.json` (`sotc-terrain/0`, S13 D8).

kirins_hill (arena_04, stage id 56): HILL_COL0..15 -> col_div16/kirins_hill_col_div16_00..15.clb -> `stage_tris`
(class != 0 or owner != 0); HILL_BOSS_COL -> col/kirins_hill_boss_col.clb -> `boss_ground` (every triangle, with
its heights and owner). Positions in the glb frame (tools/re/stage/cell_frame.py). Class/owner are the game-mask
values, from clb.stage_word (clb findings headline 3). Schema: docs/notes/s13-schemas.md section 3.

usage: terrain_export.py OUT.json [--game ROOT]
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "stage"))
import cell_frame  # noqa: E402
import clb  # noqa: E402

STAGE = "kirins_hill"
STAGE_ID = 56           # data: the .path header +0x1c cell (cell_frame.py)


def tris_of(path, tx, tz):
    """[(glb vertices, class, owner)] of every triangle in a stage-word .clb."""
    c = clb.read(path)
    words = [clb.stage_word(w) for w in c["attrs"]]
    out = []
    for s in c["shapes"]:
        V = [[round(tx - v[0], 3), round(v[1], 3), round(tz + v[2], 3)] for v in s["verts"]]
        for t in s["tris"]:
            out.append(([V[t[0]], V[t[1]], V[t[2]]],) + words[t[3]])
    return out


def export(game=clb.GAME):
    g = os.path.join(game, "cp3shadow")
    tx, tz = cell_frame.translation(STAGE_ID)
    stage = []
    for i in range(16):
        p = os.path.join(g, "col_div16", "%s_col_div16_%02d.clb" % (STAGE, i))
        stage += [{"v": v, "class": k, "owner": o} for v, k, o in tris_of(p, tx, tz) if k or o]
    boss = [{"v": v, "owner": o} for v, _, o in tris_of(os.path.join(g, "col", STAGE + "_boss_col.clb"), tx, tz)]
    return {"format": "sotc-terrain/0", "stage": STAGE, "frame": {"Tx": tx, "Tz": tz},
            "stage_tris": stage, "boss_ground": boss}


if __name__ == "__main__":
    a = sys.argv[1:]
    game = a[a.index("--game") + 1] if "--game" in a else clb.GAME
    doc = export(game)
    with open(a[0], "w") as f:
        json.dump(doc, f, separators=(",", ":"))
    print("wrote %s: %d stage_tris, %d boss_ground" % (a[0], len(doc["stage_tris"]), len(doc["boss_ground"])))
