"""clb parser, terrain export and grip-region census (S13 Task 9). Needs the game tree and the library: fails, never
skips, when either is missing (SOTC_GAME_ROOT, SOTC_LIB).

Run: SOTC_GAME_ROOT=... SOTC_LIB=... python3 -I tools/re/clb/test_clb.py
"""
import glob
import json
import os
import statistics
import struct
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.dont_write_bytecode = True
import boss_regions  # noqa: E402
import clb  # noqa: E402
import terrain_export  # noqa: E402

GAME = os.environ["SOTC_GAME_ROOT"]      # KeyError = fail, not skip
LIB = os.environ["SOTC_LIB"]
# arena_04 ground glbs (arena04 findings item 1: ground, structures_platform, ruins_entrance, ruins_skirts)
GROUND = ["arena_04_ground", "arena_04_structures_platform", "arena_04_ruins_entrance", "arena_04_ruins_skirts"]
MOUTHS = {6: (1011, 121), 7: (970, 121), 8: (970, 159), 9: (1011, 159)}


def glb_positions(path):
    """All POSITION vertices of a static glb (node transforms are identity on the level glbs)."""
    with open(path, "rb") as f:
        d = f.read()
    n = struct.unpack_from("<I", d, 12)[0]
    j = json.loads(d[20:20 + n])
    assert all(not (set(nd) & {"matrix", "translation", "rotation", "scale"}) for nd in j["nodes"]), path
    bin_ = d[20 + n + 8:]
    out = []
    for m in j["meshes"]:
        for p in m["primitives"]:
            a = j["accessors"][p["attributes"]["POSITION"]]
            bv = j["bufferViews"][a["bufferView"]]
            assert bv.get("byteStride") in (None, 12)
            o = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
            flat = struct.unpack_from("<%df" % (3 * a["count"]), bin_, o)
            out += [flat[k:k + 3] for k in range(0, len(flat), 3)]
    return out


def bbox(tris):
    pts = [p for t in tris for p in t["v"]]
    return [min(p[a] for p in pts) for a in (0, 2)], [max(p[a] for p in pts) for a in (0, 2)]


class Clb(unittest.TestCase):
    def test_all_parse(self):
        files = glob.glob(os.path.join(GAME, "cp3shadow", "**", "*.clb"), recursive=True)
        self.assertEqual(len(files), 1042)
        for p in files:
            clb.read(p)                  # raises on any index out of range

    def test_stage_word_rule(self):
        c = clb.read(os.path.join(GAME, "cp3shadow/col_div16/kirins_hill_col_div16_00.clb"))
        self.assertEqual(len(c["attrs"]), 90)
        self.assertEqual([clb.stage_word(w)[0] for w in c["attrs"][83:88]], [4, 6, 7, 8, 9])


class Terrain(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.doc = terrain_export.export(GAME)
        cls.by_class = {}
        for t in cls.doc["stage_tris"]:
            cls.by_class.setdefault(t["class"], []).append(t)

    def test_schema(self):
        self.assertEqual(self.doc["format"], "sotc-terrain/0")
        self.assertEqual(self.doc["frame"], {"Tx": 900, "Tz": 300})
        for t in self.doc["stage_tris"]:
            self.assertTrue(t["class"] or t["owner"])
            self.assertEqual(len(t["v"]), 3)

    def test_classes(self):
        self.assertEqual(set(self.by_class) - {0}, {3, 4, 5, 6, 7, 8, 9})

    def test_ring_corridor(self):
        tris = self.by_class[4]
        self.assertEqual(len(tris), 240)
        lo, hi = bbox(tris)
        self.assertTrue(980 <= lo[0] and hi[0] <= 1000 and 130 <= lo[1] and hi[1] <= 150, (lo, hi))

    def test_tunnel_mouths(self):
        for k, (mx, mz) in MOUTHS.items():
            lo, hi = bbox(self.by_class[k])
            self.assertTrue(lo[0] - 3 <= mx <= hi[0] + 3 and lo[1] - 3 <= mz <= hi[1] + 3, (k, lo, hi))

    def test_boss_ground(self):
        tris = [t for t in self.doc["boss_ground"] if t["owner"] == 0x400000]
        self.assertTrue(tris)
        lo, hi = bbox(tris)
        for got, want in zip(lo + hi, (958.5, 108.4, 1021.5, 171.6)):
            self.assertAlmostEqual(got, want, delta=0.1)

    def test_frame_residual(self):
        """Median distance from the ruins' stage-clb vertices (classes 4, 6..9) to the nearest arena_04 ground-glb
        vertex <= 0.5 m (measured 0.149, clb findings headline 5); the route frame's check too (same cell_frame).
        The ruins only: the north hill/road classes lie partly outside the ground glbs."""
        glb = []
        for n in GROUND:
            glb += glb_positions(os.path.join(LIB, "models/shadow/levels/arena_04/_cmn", n + ".glb"))
        cell = {}
        for g in glb:
            cell.setdefault((int(g[0] // 4), int(g[2] // 4)), []).append(g)
        res = []
        ruins = [v for k in (4, 6, 7, 8, 9) for t in self.by_class[k] for v in t["v"]]
        for v in ruins[::7]:
            best = 1e9
            for dx in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    for g in cell.get((int(v[0] // 4) + dx, int(v[2] // 4) + dz), ()):
                        best = min(best, ((g[0] - v[0]) ** 2 + (g[1] - v[1]) ** 2 + (g[2] - v[2]) ** 2) ** 0.5)
            if best < 1e9:
                res.append(best)
        self.assertGreater(len(res), 100)
        self.assertLessEqual(statistics.median(res), 0.5)


class BossRegions(unittest.TestCase):
    def test_kirin(self):
        r = boss_regions.regions("col/kirin_a_coli.clb", "skel/kirin_a.skb")
        self.assertIn("0x10", r["Bone_HEAD"])
        self.assertIn("0x20", r["Bone_KUBI"])
        for b in r:
            if any(k in b for k in ("MOMO", "SUNE", "TOE", "UDE", "JOWAN")):
                self.assertEqual(r[b], [], b)

    def test_knight(self):
        r = boss_regions.regions("col/knight_a_col.clb", "skel/knight_a.skb")
        want = {"Bone_UDE_R": "0xb0", "Bone_JOWA_R": "0xe0", "Bone_KATA_R": "0x60", "Bone_KUBI": "0x20",
                "Bone_KAO": "0x10", "Bone_HARA": "0x50"}     # S12 kBandRegion codes on their bones
        for bone, code in want.items():
            self.assertIn(code, r[bone], bone)


if __name__ == "__main__":
    unittest.main()
