"""coverage.py on the 16 exported graphs ($SOTC_LIB/behaviour; fails, never skips, when they are missing) and a
synthetic dump built from KNIGHT's own graph (what Gaius's registry runs): KNIGHT is complete, KIRIN misses exactly
the census16 list (findings.md headline 7).

Run: python3 -I tools/re/scr/test_coverage.py
"""
import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE]
sys.dont_write_bytecode = True
import coverage as cov  # noqa: E402

LIB = os.environ.get("SOTC_LIB", "")
GRAPHS = os.path.join(LIB, "behaviour")

KIRIN_PREDS = {"pred_6fdf70", "pred_6fdfd0", "pred_8336f0", "anger_ge", "hit_kind", "hit_region", "boss_attr_bits",
               "cloak_equipped", "hp_pct_ge", "move_done", "move_done_ap", "time_since_transition_ge"}
KIRIN_ACTS = {"act_6fc8a0", "act_6fc8c0", "act_6fccf0", "act_6fd5a0", "act_6fdf20", "act_6fdf90", "act_6fdfb0",
              "act_82e290", "act_82e2c0", "route_by_dprelate", "anger_set"}   # route_by_dprelate is 0x82e3a0


def synthetic_dump(graphs):
    """KNIGHT's predicates and actions implemented, its kinds and opcodes too, plus `sub` as the real dump has it"""
    k = graphs["KNIGHT"]
    lines = ["registry knight"]
    lines += ["%x implemented" % fn for fn in sorted(set(k["preds"]) | set(k["acts"]))]
    lines += ["kind %d" % n for n in sorted(k["kinds"])]
    lines += ["op 0x%02x implemented" % n for n in sorted(k["ops"])]
    lines += ["feature aim gap"]
    return "\n".join(lines) + "\n"


class Coverage(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not os.path.isdir(GRAPHS):
            raise AssertionError("SOTC_LIB/behaviour missing (set SOTC_LIB, run `make behaviour`): " + GRAPHS)
        cls.graphs = cov.load_graphs(GRAPHS)

    def test_sixteen_graphs(self):
        self.assertEqual(len(self.graphs), 16)

    def test_knight_and_kirin(self):
        dump = cov.parse_dump(synthetic_dump(self.graphs))
        rep = {b: cov.classify(g, dump) for b, g in self.graphs.items()}
        for cat in ("preds", "acts", "ops", "kinds", "feats"):
            self.assertEqual(rep["KNIGHT"][cat]["missing"], {}, cat)
        self.assertEqual(self.graphs["KNIGHT"]["feats"], {})   # none of his rows names an aim bone
        self.assertEqual(len(self.graphs["KNIGHT"]["preds"]), 24)
        self.assertEqual(len(self.graphs["KNIGHT"]["acts"]), 18)
        k = rep["KIRIN"]
        self.assertEqual(set(k["preds"]["missing"].values()), KIRIN_PREDS)
        self.assertEqual(set(k["acts"]["missing"].values()), KIRIN_ACTS)
        self.assertEqual(set(k["kinds"]["missing"]), {2, 6, 8, 9})
        self.assertEqual(set(k["ops"]["missing"]), {0x04, 0x4c})
        self.assertNotIn(0x832610, k["preds"]["missing"])   # `sub` is the evaluator's own
        self.assertEqual(k["feats"]["gap"], {"aim": "aim"})   # her stomps name OptBone_IKFootFL/FR (§13 ruling 4)
        self.assertEqual(rep["MAMMOTH"]["feats"]["gap"], {"aim": "aim"})

    def test_report(self):
        dump = cov.parse_dump(synthetic_dump(self.graphs))
        with tempfile.TemporaryDirectory() as d:
            out = os.path.join(d, "dump.txt")
            md = os.path.join(d, "coverage.md")
            open(out, "w").write(synthetic_dump(self.graphs))
            cov.main(["coverage.py", GRAPHS, out, md])
            text = open(md).read()
        self.assertIn("| KIRIN |", text)
        self.assertIn("| **total** |", text)
        self.assertIn("anger_ge", text)
        self.assertIn("0x4c", text)
        self.assertIn("under", text)   # ruling R-U footer
        self.assertIn("- aim — ", text)
        self.assertIn("KIRIN gap", text)
        # The frozen s13 gate's row shape (it reads KIRIN's): four category cells and the missing count.
        self.assertRegex(text, r"(?m)^\| KNIGHT \| ([0-9]+/[0-9]+/0 \| ){4}0 \|$")

    def test_gap_and_precedence(self):
        d = cov.parse_dump("registry a\n10 gap\n20 implemented\nkind 1 gap\nkind 0\nop 0x04 gap\n"
                           "registry b\n10 implemented\nkind 1 gap\nfeature aim gap\n")
        self.assertEqual(d["fns"], {0x10: "implemented", 0x20: "implemented"})
        self.assertEqual(d["ops"], {4: "gap"})
        self.assertEqual(d["kinds"], {1: "gap", 0: "implemented"})
        self.assertEqual(d["feats"], {"aim": "gap"})

    def test_aim_missing_without_the_line(self):
        dump = cov.parse_dump("registry a\nkind 1\n")
        self.assertEqual(cov.classify(self.graphs["KIRIN"], dump)["feats"]["missing"], {"aim": "aim"})


if __name__ == "__main__":
    unittest.main()
