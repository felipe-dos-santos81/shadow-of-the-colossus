"""The stage-cell -> glb frame, shared by the route (.path) and terrain (.clb) exporters (S13 design D7/D8).

A stage's game data sits in its world cell `stage_id = row * 10 + col` (.path header +0x1c); the glb frame is
x-mirrored and translated by the cell: glb = (600·col − 2700 − x, y, 600·row − 2700 + z).
Guess by pattern, fitted on kirin (56), knight (24) and minotaur (55), not traced (path findings §3); the clb
frame agrees on arena_04 with a 0.149 m median residual (measured, clb findings §5).
"""


def translation(stage_id):
    """(Tx, Tz): glb = (Tx − x, y, Tz + z)."""
    row, col = divmod(stage_id, 10)
    return 600 * col - 2700, 600 * row - 2700


def glb_from_game(stage_id, x, y, z):
    tx, tz = translation(stage_id)
    return tx - x, y, tz + z
