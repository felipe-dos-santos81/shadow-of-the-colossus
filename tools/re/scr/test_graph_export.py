"""graph_export.py against the real out/re/eboot.elf (fails, never skips, when it or SOTC_GAME_ROOT is missing).

Run: python3 -I tools/re/scr/test_graph_export.py
"""
import json
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE]
sys.dont_write_bytecode = True
import graph_export as ge  # noqa: E402

# boss: (modes, conditions, transitions, motion-graph edges)
PINNED = {"KNIGHT": (13, 53, 40, 171), "MAMMOTH": (21, 55, 105, 172), "MINOTOUR_A": (21, 65, 107, 216)}


def shape(g):
    return (len(g["modes"]), len(g["conditions"]), sum(len(m["transitions"]) for m in g["modes"]), len(g["motion_graph"]))


def clips(o, out):
    if isinstance(o, dict):
        for k, v in o.items():
            if k == "clip" and isinstance(v, str): out.add(v)
            clips(v, out)
    elif isinstance(o, list):
        for v in o: clips(v, out)
    return out


class GraphExport(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        assert os.path.isfile(ge.DEFAULT_ELF), "%s missing: run scripts/re0-acceptance.sh" % ge.DEFAULT_ELF
        ge.load()
        cls.g = {b: ge.export(b) for b in PINNED}

    def test_shapes(self):
        for b, want in PINNED.items():
            self.assertEqual(shape(self.g[b]), want, b)

    def test_format_and_keys(self):
        k = self.g["KNIGHT"]
        self.assertEqual(k["format"], "sotc-scr-graph/0")
        self.assertEqual(k["boss"], "KNIGHT")
        for key in ("root", "modes", "conditions", "actions", "control_modes", "motion_graph"):
            self.assertIn(key, k)

    def test_retry_start_mode(self):
        r = self.g["KNIGHT"]["root"]["retry_start_mode"]
        self.assertEqual(r["mode"], 3)
        self.assertEqual(r["name"], "Knight_route_patrol")
        self.assertEqual(self.g["KNIGHT"]["modes"][3]["name"], "Knight_route_patrol")

    def test_knight_clips_exist(self):
        game = os.environ.get("SOTC_GAME_ROOT")
        self.assertTrue(game, "SOTC_GAME_ROOT is unset")
        have = set(os.listdir(os.path.join(game, "cp3shadow", "anim")))
        k = self.g["KNIGHT"]
        edge = {e[s] for e in k["motion_graph"] for s in ("from", "to")} - {None}   # additive rows have no 'from'
        names = clips(k, set()) | edge
        self.assertGreater(len(names), 20)
        self.assertEqual(sorted(n for n in names if n + ".anb" not in have), [])

    def test_two_runs_byte_identical(self):
        with tempfile.TemporaryDirectory() as d:
            outs = []
            for i in (0, 1):
                p = os.path.join(d, "sub", "k%d.json" % i)       # the exporter creates the directory
                subprocess.run([sys.executable, "-I", os.path.join(HERE, "graph_export.py"), "export", "KNIGHT", p], check=True)
                with open(p, "rb") as fh:
                    outs.append(fh.read())
            self.assertEqual(outs[0], outs[1])
            self.assertEqual(json.loads(outs[0]), json.loads(json.dumps(self.g["KNIGHT"])))

    def test_missing_elf_fails_closed(self):
        r = subprocess.run([sys.executable, "-I", os.path.join(HERE, "graph_export.py"), "export", "KNIGHT", "/dev/null", "--elf", "/nonexistent.elf"],
                           capture_output=True, text=True)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("re0-acceptance.sh", r.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
