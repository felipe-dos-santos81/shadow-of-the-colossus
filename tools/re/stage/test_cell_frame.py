"""cell_frame.glb_from_game: the stage-cell -> glb frame shared by the route and terrain exporters.

Pins: the three stages the frame was fitted on (path findings §3, clb findings §5; measured).

Run: python3 -I tools/re/stage/test_cell_frame.py
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.dont_write_bytecode = True
import cell_frame  # noqa: E402


class CellFrame(unittest.TestCase):
    def test_translations(self):
        # stage id: (Tx, Tz) — glb = (Tx - x, y, Tz + z)
        for stage, (tx, tz) in {56: (900, 300), 24: (-300, -1500), 55: (300, 300)}.items():
            self.assertEqual(cell_frame.glb_from_game(stage, 0.0, 0.0, 0.0), (tx, 0.0, tz))

    def test_x_mirrored_y_kept(self):
        self.assertEqual(cell_frame.glb_from_game(56, 10.0, 52.5, -158.0), (890.0, 52.5, 142.0))


if __name__ == "__main__":
    unittest.main()
