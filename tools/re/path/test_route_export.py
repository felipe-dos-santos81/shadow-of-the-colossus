"""route_export: every .path parses (144) and kirin's route tree matches the recon numbers (path findings §1, §5; measured).

Needs SOTC_GAME_ROOT (fails, never skips). Run: python3 -I tools/re/path/test_route_export.py
"""
import math
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.dont_write_bytecode = True
import qath  # noqa: E402
import route_export  # noqa: E402

PATH_DIR = os.path.join(os.environ["SOTC_GAME_ROOT"], "cp3shadow", "path")  # KeyError when unset: fail, never skip


def load(name):
    with open(os.path.join(PATH_DIR, "pathdata", name + ".path"), "rb") as f:
        return qath.parse(f.read())


def xz(p):
    return p["pos"][0], p["pos"][2]


class Routes(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.k = route_export.export(load("kirin"), "kirin.path")

    def test_all_parse(self):
        files = [os.path.join(d, f) for d, _, fs in os.walk(PATH_DIR) for f in fs if f.endswith(".path")]
        self.assertEqual(len(files), 144)
        for f in files:
            with open(f, "rb") as fh:
                qath.parse(fh.read())

    def test_kirin_counts(self):
        k = self.k
        self.assertEqual((len(k["nodes"]), len(k["links"]), len(k["gps"]), len(k["points"])), (52, 62, 11, 17))
        self.assertEqual(sorted(r["id"] for r in k["routes"]), [2, 3, 4, 5, 6])
        self.assertEqual((k["format"], k["stage_id"], k["frame"]["Tx"], k["frame"]["Tz"]), ("sotc-route/0", 56, 900, 300))

    def test_route4_goal_points(self):
        k = self.k
        gps = [k["gps"][i] for i in next(r for r in k["routes"] if r["id"] == 4)["gps"]]
        for want in ((976.3, 126.7), (954.4, 108.1)):  # inner (ruin roof), outer (ground)
            self.assertTrue(any(math.dist(xz(g), want) < 0.2 for g in gps), (want, gps))

    def test_route2_pacing_points(self):
        k = self.k
        for gi in next(r for r in k["routes"] if r["id"] == 2)["gps"]:
            pp = [k["points"][i] for i in k["gps"][gi]["points"] if k["points"][i]["pp"]]
            self.assertEqual(len(pp), 2, gi)
            # measured 20.79 / 24.00 / 24.45 m (the findings' "21-24 m" is rounded)
            self.assertTrue(20.5 <= math.dist(pp[0]["pos"], pp[1]["pos"]) <= 24.5, gi)

    def test_hill_links_tagged(self):
        self.assertEqual(sum(1 in l["tags"] for l in self.k["links"]), 27)

    def test_link_length_is_node_distance(self):
        for name in ("kirin", "knight", "minotaur"):
            d = route_export.export(load(name), name + ".path")
            for l in d["links"]:
                self.assertLess(abs(l["length"] - math.dist(d["nodes"][l["a"]]["pos"], d["nodes"][l["b"]]["pos"])), 0.01, (name, l))


if __name__ == "__main__":
    unittest.main()
