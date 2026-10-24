"""graph_export.py against the real out/re/eboot.elf (fails, never skips, when it or SOTC_GAME_ROOT is missing).

Not wired into make/verify: it needs out/re/eboot.elf and SOTC_GAME_ROOT.

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

# boss: (modes, conditions, transitions, motion-graph edges). MAMMOTH/MINOTOUR_A gained 2/1 conditions in S13: their
# attackdata rows' conditions are walked now. KIRIN's 84 conditions are the recon's count (the walks add none).
PINNED = {"KNIGHT": (13, 53, 40, 171), "MAMMOTH": (21, 57, 105, 172), "MINOTOUR_A": (21, 66, 107, 216),
          "KIRIN": (15, 84, 97, 314)}
BOSSES = ["MINOTOUR_A", "MAMMOTH", "KNIGHT", "YAMORI_B", "BUDDHA", "MINOTOUR_B", "BIRD", "KAME", "EEL",
          "MINOTOUR_C", "KIRIN", "LEO", "NARGA", "POSEIDON", "SNAKE", "CERBERUS_R"]
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
S12_KNIGHT = os.path.join(ROOT, "out", "library", "behaviour", "KNIGHT.graph.json")   # the stored S12 export (read-only)
# S12 R2: the 28 opcodes of Gaius's 67 control-defs; the graph recon adds 0x04 and 0x4c for Phaedra
R2_OPS = {0x36, 0x38, 0x54, 0x56, 0x59, 0x5c, 0x76, 0x42, 0x43, 0x45, 0x3a, 0x64, 0x3b, 0x28, 0x27,
          0x67, 0x6c, 0x78, 0x7a, 0x7c, 0x7e, 0x80, 0x7d, 0x69, 0x6e, 0x71, 0x74, 0x83}
SLOW_ADDED = ["ref_player", "track", "judge", "p60", "p64", "p68", "p6c", "p70", "p74", "p98", "p9c"]


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
        cls.g = {b: ge.export(b) for b in BOSSES}

    def test_shapes(self):
        for b, want in PINNED.items():
            self.assertEqual(shape(self.g[b]), want, b)

    def test_format_and_keys(self):
        k = self.g["KNIGHT"]
        self.assertEqual(k["format"], "sotc-scr-graph/0")
        self.assertEqual(k["boss"], "KNIGHT")
        for key in ("root", "modes", "conditions", "actions", "control_modes", "motion_graph"):
            self.assertIn(key, k)

    def test_root_control_modes_actions(self):
        k = self.g["KNIGHT"]
        r = k["root"]
        self.assertEqual((r["start_mode"]["mode"], r["start_mode"]["name"]), (0, "Knight_standby_mode"))
        self.assertEqual((r["death_mode"]["mode"], r["death_mode"]["name"]), (8, "Knight_death"))
        self.assertEqual(r["retry_ctrl"]["ctrl"], "Start_Cancel")
        self.assertTrue(r["death_control_mode"])
        self.assertTrue(r["callback_actions"] and r["every_frame_actions"])
        self.assertEqual(len(k["control_modes"]), 11)
        self.assertEqual(sum(len(c["assign"]) for c in k["control_modes"].values()), 573)   # the last mode has no rows
        self.assertEqual(len(k["actions"]), 21)
        self.assertTrue(all(isinstance(a["fn"], str) for lst in k["actions"].values() for a in lst))
        fns = {c["fn"] for cond in k["conditions"].values() for c in cond["clauses"]}
        self.assertIn("0x832610", fns)                      # 'sub': a condition nested in another

    def test_bad_boss_fails_before_reading_elf(self):
        r = subprocess.run([sys.executable, "-I", os.path.join(HERE, "graph_export.py"), "export", "NOPE", "/dev/null"],
                           capture_output=True, text=True)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("unknown boss", r.stderr)

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
        for b, g in self.g.items():                          # all 16, edge refs included (the loader refuses a missing clip)
            names = clips(g, set()) | {e[s] for e in g["motion_graph"] for s in ("from", "to", "ref")} - {None}
            self.assertEqual(sorted(n for n in names if n + ".anb" not in have), [], b)

    def test_all_16_export(self):
        self.assertEqual(set(self.g), set(BOSSES))
        for b, g in self.g.items():
            self.assertEqual(g["boss"], b)
            self.assertTrue(g["modes"] and g["motion_graph"] and g["conditions"], b)
            json.dumps(g, allow_nan=False)                  # the app's parser takes no NaN/Infinity
            for key in ("control_defs", "dprelate", "attackdata"):
                self.assertIsInstance(g[key], dict, b)

    def test_s13_additions_leave_the_s12_export_untouched(self):
        self.assertTrue(os.path.isfile(S12_KNIGHT), "%s missing" % S12_KNIGHT)
        with open(S12_KNIGHT) as fh:
            old = json.load(fh)
        new = json.loads(json.dumps(self.g["KNIGHT"]))
        for key in ("control_defs", "dprelate", "attackdata"): del new[key]
        del new["root"]["kind"]
        for m in new["modes"]:
            for k in SLOW_ADDED:
                if m["locomotion"]: del m["locomotion"][k]
        for e in new["motion_graph"]:
            for k in ("ref", "speed", "blend_out"): del e[k]
        self.assertEqual(new, old)

    def test_kirin(self):
        k = self.g["KIRIN"]
        self.assertEqual(k["root"]["retry_start_mode"]["mode"], 2)
        self.assertEqual(k["root"]["death_mode"]["mode"], 5)
        args = [cl["args"] for c in k["conditions"].values() for cl in c["clauses"] if cl["pred"] == "pred_6fdfd0"]
        self.assertTrue(args)
        self.assertTrue(all(a == {"i10": 30, "i14": 5} for a in args), args)    # fix 2: typed raw keys, not dropped
        rows = k["dprelate"]["KIRIN:scr_dprelate[0]"]
        self.assertEqual({int(r["value"], 16) >> 48: r["route"] for r in rows}, {2: 1, 3: 2, 6: 3, 7: 4, 8: 5, 9: 6})
        self.assertTrue(all(r["mask"] == "0x7f000000000000" and r["object"] is None and r["cond"] is None for r in rows))
        # the loop's reference is data +0x98 (recon additive adddefs.json): neck_refuse_start
        refs = {e["ref"] for e in k["motion_graph"] if e["table"] == "KIRIN_ADD" and e["to"] == "kirin_a_add_neck_down_2_loop"}
        self.assertEqual(refs, {"kirin_a_add_neck_refuse_start"})
        self.assertTrue(any(e["speed"] for e in k["motion_graph"]))

    def test_control_defs(self):
        for b, extra in (("KNIGHT", set()), ("KIRIN", {0x04, 0x4c})):
            g = self.g[b]
            whens = {r["when"] for cm in g["control_modes"].values() for r in cm["assign"] if r["when"]}
            self.assertEqual(set(g["control_defs"]), whens, b)
            self.assertEqual(len(whens), {"KNIGHT": 67, "KIRIN": 66}[b])
            ops = {r["op"] for recs in g["control_defs"].values() for r in recs}
            self.assertEqual(ops - R2_OPS, extra, b)             # KIRIN uses 25 of the 28 R2 opcodes, plus 0x04 and 0x4c
            for recs in g["control_defs"].values():
                self.assertTrue(recs and all(set(r) == {"op", "min", "max", "handle", "mask", "value", "expect"} for r in recs))
                self.assertTrue(all(isinstance(r["min"], float) and isinstance(r["max"], float) for r in recs))
        (key,) = [k for k in self.g["KNIGHT"]["control_defs"] if k.startswith("KNIGHT_CONTROL:control-def[20] ")]
        recs = self.g["KNIGHT"]["control_defs"][key]
        self.assertEqual([r["op"] for r in recs], [0x38, 0x36])
        self.assertEqual((recs[0]["handle"]["ctrl"], recs[0]["handle"]["bit"]), ("Failure_issue_knight_attack", 6))
        self.assertEqual(recs[1]["handle"], "KNIGHT:control-mode[7]")
        self.assertEqual([r["expect"] for r in recs], [1, 1])

    def test_knight_attackdata_is_s12s_kattack(self):
        k = self.g["KNIGHT"]
        key = k["modes"][9]["locomotion"]["attack"]
        self.assertEqual(key, "KNIGHT_SWORD:scr_attackdata[0]")
        self.assertEqual(k["modes"][10]["locomotion"]["attack"], "KNIGHT_SWORD:scr_attackdata[4]")
        rows = k["attackdata"][key]
        self.assertEqual(len(rows), 3)                       # the terminator row is not exported
        want = [("KNIGHT_LEG_L_ATK_FIX", "KNIGHT_LEG_L_ATK", "KNIGHT_LEG_L_ATK_ATTAIN", 3, "knight_a_atk_leg_l_start", 5, 15, 0, 0),
                ("KNIGHT_LEG_R_ATK_FIX", "KNIGHT_LEG_R_ATK", "KNIGHT_LEG_R_ATK_ATTAIN", 2, "knight_a_atk_leg_r_start", 5, 15, 0, 0),
                ("KNIGHT_WEAPON_ATK_FIX", "KNIGHT_WEAPON_ATK", "KNIGHT_WEAPON_ATK_ATTAIN", 4, "knight_a_atk1_start", 15, 10, 1, 1)]
        for r, w in zip(rows, want):
            self.assertEqual((r["fix"], r["atk"], r["attain"], r["issue"]["bit"], r["motion"]["clip"], r["reselect_s"],
                              r["cooldown_s"], r["issue_arrived"], r["default"]), w)
            self.assertTrue(r["cond"] is None and r["alt_cond"] is None and r["chain"] is None and r["sensing"] == 0)
        self.assertEqual(len(k["attackdata"]["KNIGHT_SWORD:scr_attackdata[4]"]), 3)
        self.assertEqual(len(k["conditions"]), 53)
        # every mode's attack handle (any boss) has its run, and every condition handle in the new keys is walked
        for g in self.g.values():
            for m in g["modes"]:
                if m["locomotion"] and m["locomotion"]["attack"]:
                    self.assertIn(m["locomotion"]["attack"], g["attackdata"])
            for rows in g["attackdata"].values():
                for r in rows:
                    for c in (r["cond"], r["alt_cond"]): self.assertTrue(c is None or c in g["conditions"], c)
            for rows in g["dprelate"].values():
                for r in rows: self.assertTrue(r["cond"] is None or r["cond"] in g["conditions"])

    def test_loader_constraints(self):
        for b, g in self.g.items():
            def walk(o):
                if isinstance(o, dict):
                    if "bit" in o: self.assertTrue(0 <= o["bit"] <= 63, (b, o))
                    if isinstance(o.get("motion"), str): self.assertTrue(o["clip"], (b, o))
                    for v in o.values(): walk(v)
                elif isinstance(o, list):
                    for v in o: walk(v)
            walk(g)
            for m in g["modes"]:
                if m["locomotion"]:
                    self.assertTrue(all(isinstance(m["locomotion"][k], (int, float)) for k in SLOW_ADDED if k != "judge"))
            for e in g["motion_graph"]:
                self.assertTrue(e["ref"] is None or isinstance(e["ref"], str))
                self.assertIsInstance(e["speed"], bool)

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
