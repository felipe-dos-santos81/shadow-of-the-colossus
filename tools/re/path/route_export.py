#!/usr/bin/env python3
"""Export a `.path` file as `sotc-route/0` JSON, positions in the glb frame (S13 Task 7; schema docs/notes/s13-schemas.md §2).

  route_export.py FILE.path OUT.json
"""
import json, os, sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE, os.path.join(os.path.dirname(HERE), "stage")]
import qath
import cell_frame


def export(data, source):
    sid = data["stage_id"]
    tx, tz = cell_frame.translation(sid)
    g = lambda p: [round(v, 4) for v in cell_frame.glb_from_game(sid, *p)]
    return {
        "format": "sotc-route/0", "source": source, "stage_id": sid,
        "frame": {"Tx": tx, "Tz": tz, "label": "glb = (Tx - x, y, Tz + z)"},
        "nodes": [{"i": n["i"], "pos": g(n["pos"]), "class": n["class"], "tag": n["tag"], "links": n["links"]}
                  for n in data["nodes"]],
        "links": [{"i": l["i"], "a": l["a"], "b": l["b"], "flags": l["flags"], "tags": l["tags"],
                   "length": round(l["length"], 4)} for l in data["links"]],
        "routes": [{"id": r["key"], "pos": g(r["pos"]), "gps": r["list"]} for r in data["routes"]],
        "gps": [{"i": p["i"], "pos": g(p["pos"]), "points": p["list"]} for p in data["gps"]],
        "points": [{"i": p["i"], "pos": g(p["pos"]), "ap": p["ap"], "pp": p["pp"], "angle": round(p["angle"], 4),
                    "gp": p["gp"]} for p in data["points"]],
    }


if __name__ == "__main__":
    src, dst = sys.argv[1:3]
    doc = export(qath.parse(open(src, "rb").read()), os.path.basename(src))
    os.makedirs(os.path.dirname(os.path.abspath(dst)), exist_ok=True)
    with open(dst, "w") as f:
        json.dump(doc, f, separators=(",", ":"))
    print("%s: %d nodes %d links %d routes %d gps %d points" % (dst, *(len(doc[k]) for k in ("nodes", "links", "routes", "gps", "points"))))
