# S13 schemas (2026-10-24) — the `sotc-scr-graph/0` additions, `sotc-route/0`, `sotc-terrain/0`

The contracts the S13 lanes write and read (spec `docs/superpowers/specs/2026-10-24-s13-phaedra-systems-design.md`
D2/D7/D8 and §13; plan Task 1). Every file here is derived from the game and lives in the library
(`$SOTC_LIB`), never in git. Labels: data / binary / measured / guess.

## 1. `sotc-scr-graph/0` additions (`<SOTC_LIB>/behaviour/<BOSS>.graph.json`)

The label stays `/0` (ruling §13.1): every key below is **additive and optional** — a document without it
loads exactly as before (`scr_graph_load`, `app/src/scr_graph.cpp`). Inside a key, every field of a record
or row must be present (an explicit `null` is fine), as for the mode blocks. Handles use the exporter's
`ref()` form (`tools/re/scr/graph_export.py`): `{"ctrl", "bit", "index"}` (a control; the loader keeps the
request bit), `{"motion", "name", "clip"}` (a motion; the loader resolves the clip, a clip missing from the
clip names is a **load error**), `{"mode", "name"}` (a mode index), `"TABLE:Cn"` (a condition, which must be
in `conditions`: the exporter walks it), any other string (a handle the app keeps as text), or `null`.
Words (`mask`, `value`) are `"0x…"` strings (u64); `-1` is accepted for all ones.

### `control_defs`

```json
"control_defs": {"<when>": [{"op": 56, "min": 0.0, "max": 0.0, "handle": <ref>, "mask": "0x…", "value": "0x…", "expect": 1}]}
```

- Key: the control-def handle **exactly as a control-assign row's `when` spells it**
  (`"COMMON_CONTROL:control-def[56] 'GENERAL_PURPOSE_BATSU_BUTTON_INPUT_DETECTION'"`); one entry for every
  control-def any control-assign row names, base and additive.
- Records: R2's 0x68-byte records (`docs/notes/s12-recon.md` R2, binary) in table order up to (not including)
  the opcode-1 terminator: `op` +00 (a number), `min`/`max` +04/+10 (floats), `handle` +38 (a ref), `mask`
  +48, `value` +50, `expect` +58 (number or bool).
- Loaded into `ScrGraph::controlDefs` (`ScrCdefRecord`; the handle resolved as an argument). Task 4's
  `scr_cdef_eval` is the AND of `(result == expect)` over the records.

### `dprelate`

```json
"dprelate": {"<handle>": [{"mask": "0x…", "value": "0x…", "object": <string|null>, "cond": "TABLE:Cn"|null, "route": 3}]}
```

- Key: the `scr_dprelate` handle exactly as the naming action's argument spells it; one entry for every
  table an action names (KIRIN: data 0x22d37e0). `cond` is walked into `conditions`.
- Loaded into `ScrGraph::dprelate` (`ScrDprelateRow`). Task 8's `route_by_dprelate` takes the first row whose
  `(playerGroundAttr & mask) == value` and whose `cond` holds.

### `attackdata`

```json
"attackdata": {"KNIGHT_SWORD:scr_attackdata[0]": [{"label", "chain", "chain_pct", "ctrl_modes", "default",
  "reselect_s", "sensing", "cond", "fix", "atk", "alt_atk", "attain", "alt_cond", "issue", "issue2", "motion",
  "aim_bone", "aim_volume", "f0", "cooldown_s", "issue_arrived"}]}
```

- Key: the slowstatus `attack` handle **exactly as `locomotion.attack` exports it today** (e.g.
  `KNIGHT_SWORD:scr_attackdata[0]`); one entry per handle any mode's slowstatus names.
- Rows (census16 findings "Attackdata export", data; 0x100 B each) from the named row to the terminator (a
  row with no motion and no FIX, not exported): `label` +00 text; `chain` +08 handle text; `chain_pct` +18;
  `ctrl_modes` +20 handle text; `default` +30 (0/1); `reselect_s` +34; `sensing` +38; `cond` +40
  (`TABLE:Cn`, walked); `fix`/`atk`/`alt_atk` +50/+60/+70 judge names; `alt_cond` +80 (`TABLE:Cn`); `attain`
  +90 judge name; `issue`/`issue2` +a0/+b0 controls; `motion` +c0 a motion ref (its clip must exist);
  `aim_bone`/`aim_volume` +d0/+e0 names (the foot-IK aim: a gap, ruling §13.4); `f0` +f0; `cooldown_s` +f4;
  `issue_arrived` +f8 (0/1).
- Loaded into `ScrGraph::attackdata` (`ScrAttackRow`). S12's hand-transcribed `kAttack` is deleted by Task 5.

### Field additions

| where | key | offset | type | loaded into |
|---|---|---|---|---|
| `modes[].locomotion` | `ref_player` | slowstatus +04 | 0/1 | `ScrLocomotion::refPlayer` (route GP nearest the player, else the boss) |
| | `track` | +08 | 0/1 | `track` |
| | `judge` | +78 | handle text | `judge` |
| | `p60` `p64` `p68` `p6c` `p70` `p74` `p98` `p9c` | +60…+74, +98, +9c | number | the same names |
| `motion_graph[]` | `ref` | motion-def +0x98 | clip name or null | `ScrMotionEdge::ref` (clip index; a missing clip is a load error) |
| | `speed` | +0x60 bit 1 | bool | `speed` |
| | `blend_out` | +0x218 | frames | `blendOut` |
| | `from_ref` | the `from` motion-def's +0x98 | clip name or null | `fromRef` (−1 null; −2 when absent: any). Two motion-defs can play one clip (KIRIN's neck dummy and swing start); the additive from-rows tell them apart by it (S13 Task 14) |
| `root` | `kind` | +0x00 (guess, census16 Open) | number | `ScrRoot::kind` |

The loader also refuses a mode whose `locomotion.kind` is not in `ScrRegistry::kinds` (D4).

## 2. `sotc-route/0` (`<SOTC_LIB>/behaviour/<stem>.route.json`, Task 7)

The path findings' "Design inputs" (`.superpowers/sdd/2026-10-23-s13-recon/path/findings.md`), positions
already in the glb frame (`tools/re/stage/cell_frame.py`, stage id from the `.path` header +0x1c):

```json
{"format": "sotc-route/0", "source": "<path file>", "stage_id": 56,
 "frame": {"Tx": 900, "Tz": 300, "label": "glb = (Tx - x, y, Tz + z)"},
 "nodes":  [{"i": 0, "pos": [x, y, z], "class": 1, "tag": 0, "links": [0, 3]}],
 "links":  [{"i": 0, "a": 0, "b": 1, "flags": 1, "tags": [1, 0], "length": 12.3}],
 "routes": [{"id": 2, "pos": [x, y, z], "gps": [8, 9, 10]}],
 "gps":    [{"i": 0, "pos": [x, y, z], "points": [9]}],
 "points": [{"i": 0, "pos": [x, y, z], "ap": true, "pp": true, "angle": 180.0, "gp": 9}]}
```

`class` = node +18 class bits; `links` = the node's incident link indices; link `flags` = the live flags
(+08; 0x4 closed, 0x1/0x2 walker class); `tags` = the two i16 tags (+10); `length` = +18. Sections S5–S7 are
dropped. Node y is 1.3–8.8 m above the glb ground (measured): the app snaps it (ruling §13.2).

## 3. `sotc-terrain/0` (`<SOTC_LIB>/stage/<arena>.terrain.json`, Task 9)

```json
{"format": "sotc-terrain/0", "stage": "kirins_hill", "frame": {"Tx": 900, "Tz": 300},
 "stage_tris":  [{"v": [[x, y, z], [x, y, z], [x, y, z]], "class": 6, "owner": 0}],
 "boss_ground": [{"v": [[x, y, z], [x, y, z], [x, y, z]], "owner": 4194304}]}
```

- Positions in the glb frame (`cell_frame`). `stage_tris`: every stage-collision triangle with class ≠ 0 or
  owner ≠ 0 (unclassified ground is not exported). `boss_ground`: every triangle of the boss-only ground layer
  (`HILL_BOSS_COL`) with its owner and **its heights** (ruling §13.2: Phaedra stands on it).
- `class` and `owner` are **game-mask values**, not the stored words: the stage words are one bit low, so
  `class = (w >> 47) & 0x7f`, `owner = (w << 1) & 0x3c00000` (clb findings headline 3, measured on five
  stages); boss words are unshifted. The app forms one word per surface, `class << 48 | owner`, into
  `ScrWorld::playerGroundAttr` / `bossGroundAttr`, which `player_surface`, `boss_attr_bits` and op 0x04 mask
  as the game does. Gaius's glue keeps his S12 stand-in through the same word (owner 0x800000 on the arena
  ground, authored).

## 4. The registry dump (`scr_registry_dump`, `app/build/scr_registry_dump`)

One line per item, the coverage report's input (D13): `registry <name>` heads each registry's block (the tool
prints `knight`, then `kirin`); `<fn hex> implemented|gap` per fn (lower-case hex, no `0x`; sorted; `sub`
832610 always, the evaluator's own), `kind <n>` per locomotion kind (sorted), `op 0x<hh> implemented|gap` per
evaluator opcode (`kScrCdefOps`, filled by Task 4).
