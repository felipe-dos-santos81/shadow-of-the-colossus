# RE-2 T8 — the flag-set **with-aux population** + the **case-family coverage** (2026-10-19)

T8 of the RE-2 cycle (recon memo `docs/notes/re2-recon.md` §6, T8): close the two decode questions
T2/T3 left open — the **872 flag-set-with-aux population** (`re2-flagset.md` §7: "the `+0x08` u32
array's meaning"; `re2-sampler.md` §6: "the file-level rule that selects them is not") — and the
**flag-set case-family coverage beyond the gate's `caseD_9` family** (T4's gate ran the `caseD_9`
path only; `re-anb.md` §7.3). Everything below is measured with the committed readers
(`tools/re/anb/records.py`, `parse2.py`), raw-byte scripts over the read-only game tree, and
Ghidra 12.1.4 headless against a **copy** of the RE-2 sampler project. This file is **new** —
`re2-flagset.md`, `re2-sampler.md` and `re-anb.md` are untouched.

| | |
|---|---|
| Date | 2026-10-19 (run stamps UTC) |
| Machine | Apple M3 Max, macOS 27.0.1, arm64; `python3` 3.12 (stdlib only) |
| Repo / branch | `sotc-native`, `re2-aux` (base `036160b` = main after the RE-2 sweep) |
| Game tree (read-only) | `SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game` |
| Ghidra project (copy) | `out/re/ghidra` (copy of the `re2-sampler` project — read-only source), program `eboot.elf`, image base 0, VA→file = `+0x4000` |
| Ghidra logs (this worktree) | `out/re/log-t8-1.log` (callers of `FUN_005fd860`; decompiles `FUN_0067d8c0`, `FUN_0067cf00`, `FUN_005fd900`, `FUN_0066f9a0`, `FUN_005fd860`, `FUN_0066ecd0`), `log-t8-2.log` (disasm `0x0067d8c0`–`0x0067d9a0`, `0x0067d3e0`–`0x0067d8c0`), `log-t8-3.log` (disasm `0x67d917`–`0x67e800` — the apply case bodies), `log-t8-4.log` (decompiles `FUN_00660fc0`, `FUN_005fc0e0`, `FUN_0067bb80`, `FUN_006798b0`, `FUN_00679b10`; callers of `FUN_0067d8c0`), `log-t8-5.log` (`FUN_005fc100`; callers of `FUN_0067d3e0`, `FUN_0067bb80`) |
| Scratch (gitignored) | `out/re/aux/aux_identities.py` (+ `aux-identities.txt`), `population.py` (+ `population.json`), `families.py` (+ `families.txt`), `classify.py`, `predicate.py`, `pairs.py`, `names2.py`, `per_track.py`, `wordbits.py`, `features.py`, `exceptions.py`, `name_aux.py`, `fun_0067d8c0.c` |
| Inputs | all 4,455 `.anb`; the pinned samples (`objanim_leo_a_jump_fall`, `objanim_cerberus_a_atk_near_l`, `objanim_ath_damage`, `objanim_knight_a_*`, `objanim_buddha_a_*`, `camera_demo_end07_cut007`, `objanim_harpoon_shoot`) |

```bash
export SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
python3 tools/re/anb/records.py verify                      # 20/20 PASS, exit 0 (re-run, §5)
python3 out/re/aux/aux_identities.py                        # the counts + layout identities (§5)
python3 out/re/aux/families.py                              # the case-family coverage (§3)
# Ghidra (the RE-0 dot-free workaround; project copy in this worktree):
GHIDRA_HOME=$HOME/code/mine/ghidra-bundle/dist/ghidra_12.1.4_PUBLIC
proj_link_dir=$(mktemp -d); ln -s "$PWD/out/re/ghidra" "$proj_link_dir/ghidra"
"$GHIDRA_HOME/support/analyzeHeadless" "$proj_link_dir/ghidra" sotc -process eboot.elf \
  -noanalysis -scriptPath "$PWD/out/re/scripts" \
  -postScript FindSampler.java callers 0x005fd860 1 cap=60 \
  -postScript FindSampler.java decompile 0x0067d8c0 0x0067cf00 0x005fd900 0x0066f9a0 0x005fd860 0x0066ecd0 \
  > out/re/log-t8-1.log 2>&1; rm -rf "$proj_link_dir"
# further runs: out/re/t8-run{2..5}.sh (the same pattern; §6 lists the modes)
```

## 1 · The population rule (MEASURED)

**The predicate is the field itself, and it is the *only* file-local signal.**
`u32@(motion+0x08) != 0` → the file carries the aux array; `== 0` → it does not.

| population (4,455 `.anb`, 0 parse errors) | count | share |
|---|---|---|
| flag-clear (bit 16 clear) | 2,994 | 67.2 % |
| flag-set (bit 16 set) | 1,461 | 32.8 % |
| — flag-set **with aux** (`+0x08 != 0`) | **872** | 59.7 % of flag-set |
| — flag-set **without aux** (`+0x08 == 0`) | **589** | 40.3 % of flag-set |

(T2 §1's 872/1,461 reproduced exactly; the recon's 1,461/32.8 % likewise.)

**The layout identity (measured, 0 exceptions).** For the 872 with-aux files the field points at the
**record array's end** and the aux array sits *between* the record array and the motion structure:

```
data region … [records: 12·tracks] [aux: 4·tracks] [structure: 0x20]   ← structure ends at t1p
                ↑ motion+0x10                    ↑ motion+0x08        ↑ motion (file[0x110+u32@0x10])
```

- with aux: `+0x08 == +0x10 + 12·tracks` in **872/872**; `struct == +0x08 + 4·tracks` in **872/872**;
- without aux: `+0x08 == 0` in **589/589** and `struct == +0x10 + 12·tracks` in **589/589** (no gap);
- the structure is the **last 0x20 bytes of the data region** — `struct + 0x20 == t1p` in
  **1,461/1,461** flag-set files (so the records/aux/structure block abuts table 1);
- no record pointer (`p1`/`p2` of all 11,095 flag-set records) lands inside the aux region
  (0/11,095) or outside `[data_base, t1p)` (0/11,095) — the reader cannot collide with it.

**The semantic rule (why the array exists) — INFERRED from the consumers, MEASURED in the code.**
The aux is the **per-track node-binding table**: each u32 is a *node reference* that the runtime's
**apply** side (not the sampler) uses to place the track's sampled value on a node of the animated
object (§2). The array is therefore present iff the clip *binds its tracks to nodes* — a
**bake-side property of the clip**, and it is **not a function of any file-local content**:

- **same type sets on both sides**: e.g. 1-track type-13 files split 16 aux / 1 noaux
  (`objanim_player_a_atk1` vs `objanim_harpoon_shoot`); the (13,)-type-set group is 29 aux / 2 noaux;
- **same name+type on both sides**: 41 p1 names appear in both aux and noaux files — e.g.
  `'MAM_FOOT'` type 29 in 58 aux files and 2 noaux files (`objanim_boss_a_boot` vs
  `objanim_boss_a_mammoth_boot_camera_01_triger`), `'sword'` type 1 in 2 aux / 12 noaux,
  `'st15a_towerC2_breaksB_*'` type 1 on both sides;
- **same version on both sides**: v12 is 801 aux / 522 noaux; the v4/v5 files are all noaux
  (9/9), v7 is 3 aux / 13 noaux; the version word's high half is `0x0001` (bit 16 only) in
  **1,461/1,461** — no second flag bit distinguishes them;
- **same rate field on both sides** (Q12 rate 0 = 609 aux / 396 noaux; 0.5 = 174/96), with two
  one-sided tails worth recording: rates 0.10–0.29 occur in **76 files, all noaux**; rates
  0.39–0.46 occur in **24 files, all aux** — correlates, not the rule;
- a brute-force search over type-presence OR-sets (sizes 2–4) peaks at **1,385/1,461** accuracy
  (best set `{13, 29, 36, 44}`) — no small type rule exists.

**The correlates (measured, useful for triage).** The split follows the *object class*:

| class | with aux | without aux |
|---|---|---|
| `camera_*` (453) | **449** | 4 (`camera_debug_camera`, `camera_jitter_uma_{run,walk,waverun}` — all **v7**, the oldest camera bakes) |
| `objanim_*` (1,008) | 423 | 585 |
| — creature families | bird 26/28, minotaur 72/75, kame 42/45, kirin 38/44, yamori 46/55, knight 31/37, player 23/26, poseidon 40/51, boss 32/48, eel 7/11, narga 6/11, snake 10/17, leo 21/39, cerberus 8/15, buddha 3/24, dormin 7/7, guard 2/2, horse 4/4, namazu 1/1, smoleman 1/1 | |
| — scenery/effect families | 0/… (demo 2/81, home 0/51, ruins 0/34, ground 0/15, parthenon 0/14, tree 0/10, arrow 0/8, harpoon 0/5, tortoise 0/7, stage tokens 0/…) | |
| — mixed families | the object's *own* clips (`objanim_kame_a_*`, `objanim_poseidon_a_*`) | its *effects/props/parts/triggers/regions* (`kame_atk_dmg`, `poseidon_pulse_*`, `leo_torch_*`, `buddha_a_skirt_*`, `*_region`, `*_camera_trigger`, `*_cam_*`) |

Twenty objanim files carry the array with **all values `0xFFFFFFFF`** (`objanim_player_a_atk1`,
`…_avoid`, `…_dive_drop_atk`, `…_hang_atk_*`, `…_slope_jmp_atk`, `…_damage_h_fly1`,
`…_demo_end02_cut016_sword`/`cut020`, `objanim_horse_a_skelton`, `objanim_narga_a_skelton`,
`objanim_leo_a_jump`, `objanim_bird_a_stand`, `objanim_guard_a_pickup_*`, `objanim_poseidon_a_boot`)
— so "at least one non-`-1` value" is **not** the writer's condition either; `-1` is a *real*
reference (the node's own slot, §2), and the *absence* of the array is equivalent to all-`-1`
(the getter's default).

## 2 · The aux structure and its consumers (MEASURED)

**The getter (the reader T2 §1 did not find).** `FUN_005fd860(motion, track)` — a per-track getter
with default `-1`:

```c
/* FUN_005fd860(param_1 = motion, param_2 = track) — verbatim, out/re/log-t8-1.log */
uVar1 = *(uint *)(param_1 + 8);                       /* the +0x08 field */
uVar4 = 0;
if ((int)uVar1 < 0) uVar4 = -DAT_02f3b8c0;            /* the 31-bit resolution (load base) */
uVar2 = 0xffffffff;                                   /* default: -1 */
if ((uVar1 & 0x7fffffff) != uVar4) {                  /* field resolves non-null */
  lVar3 = ((int)uVar1 < 0) ? DAT_02f3b8c0 : 0;
  uVar2 = *(undefined4 *)((uVar1 & 0x7fffffff) + lVar3 + (long)param_2 * 4);   /* u32 per track */
}
return uVar2;
```

**Its only caller is `FUN_0067d3e0`** (callers walk: 1 edge) — the object-side sampler caller
(itself called by the character state machine `FUN_00636650`, plus `FUN_00634600` and
`FUN_00747360`). At the call site (`0x0067d7d0`, `log-t8-2.log`) the value is stored and passed as
the **5th argument of the apply call** `FUN_0067d8c0`; `FUN_0067d3e0`'s *other* per-track loop
passes a constant (`0xffffffff` / `0`, two init sites `0x0067d4d6` / `0x0067d60d`) — i.e. the
engine only reads the table on one of its two paths.

**The camera cases read the same array inline** (types 36/37, `FUN_005fd900` case `0x23`/`0x24`,
verbatim shape at `log-t8-1.log`): `uVar17 = *(uint *)(param_4 + 8); … iVar9 = -1; if (resolves)
iVar9 = *(int *)((uVar17 & 0x7fffffff) + base + lVar15 * 4);` → `param_3[0x10] = iVar9` (case
`0x23`, the camera id) / `param_3[0xc] = iVar9` (case `0x24`). **This is the only aux read inside
the sampler** — the `caseD_9` body (`FUN_00600ef0`) never sees it (T2 §4 re-verified).

**The apply side consumes it as a node reference.** `FUN_0067d8c0` (the apply dispatch; 3 callers:
`FUN_006789c0`, `FUN_0067cf00`, `FUN_0067d3e0`) switches on the *record type* (`*out_block`, from
`FUN_005fd900`'s `out[0]`) with a **48-entry jump table at `0x01270df0`** (types 1…48; 8, 32, 36,
37, 38 → the default) and passes the aux into the per-type bodies. The bodies use it identically
(verbatim, `FUN_0067bb80` = the type 3/10/13 apply; the same pattern in the type 29/30/31 bodies,
`log-t8-4.log` / `log-t8-3.log`):

```c
if ((aux & 0x80010000) == 0) {                        /* plain node index */
  matrix = (float *)((int)aux * 0x40 + *(long *)(node + 0x1a0));    /* 4x4 node matrices, stride 0x40 */
  quatpos = (float *)(*(long *)(node + 400) + 0x10 + (int)aux * 0x20);   /* node transforms, stride 0x20 */
} else {
  FUN_005fc0e0(out, out2, obj, aux, 1);               /* the special-reference path */
}
```

with `node = FUN_00660fc0(obj)` (the object argument; a node-type dispatch: byte `+0xc` ∈ {9, 10,
0xb, 0xd, 0x11, 0x13, 0x16} → a sub-structure).

**The special values are decoded by `FUN_005fc100`** (the `FUN_005fc0e0` target; verbatim,
`log-t8-5.log`):

```c
if (0 < aux && (aux & 0x10000)) { FUN_00616c20(…, node, aux & 0xfffeffff, …); return; }  /* bit 16 = indirection */
if (aux == 0xfffffffd /* -3 */) { … node+0x150 (quat), node+0x640 (pos) … }
if (aux == 0xffffffff /* -1 */) { … node+0x630 (pos), node+0x160 (quat) … }
if (aux == 0xfffffffe /* -2 */) { … node+0x150 (quat) … }
FUN_006163a0(&out, &out2, node, aux, param_6);        /* the general node-id resolution */
```

So the value vocabulary is **structural, not opaque**: `-1`/`-2`/`-3` are *self-references* to
specific transform slots of the resolved node; `bit 16` is an *indirection* flag (cleared and
re-dispatched); other positives are *node ids* resolved by `FUN_006163a0`. The data agrees: e.g.
`objanim_buddha_a_death`'s 15 `'EVIS_PIECE_FALL'` tracks carry **distinct** ids up to `0x4B`
(the shattered boss's pieces — a ~76-node model), while `objanim_buddha_a_skelton`'s four
`'BUDDHA_PULSE'` tracks carry `-3` (a shared self-slot).

**Value vocabulary (corpus, 3,928 values over 872 files; 63 distinct).** `0xFFFFFFFF` ×789,
`0xFFFFFFFD` ×167, small ids `0..75` (gaps at 32, 34, 36, 43–49, 52–56, 58, 61, 63, 66, 68, 70–74),
and `bit-16 + id` `0x10000…0x1000C` (10 distinct). Per type (top values): 1 → `-1`×130;
2 → `-1`×34, 1×11, 10×9; 9 → 12×56, `-1`×34, 20×24; 11 → 19×84, 15×82, 0×79; 13 → 20×30, 27×24,
25×21; 29 → 8×220, 19×172, 4×119; 36 → `-1`×414, `0x10006`×13, `0x10007`×6; 44 → `-3`×155,
`-1`×34; 46 → `-1`×30, `-3`×11. (Full tables: `out/re/aux/aux-identities.txt`.)

**T3's caller question, answered at the mechanism level.** `FUN_0060b720`'s gate
(`motion+0x08 == 0` → the dispatch loop) means the **589** no-aux files take the bulk loop's
path A (the per-type dispatch) and the **872** aux files would be misread by path B (their aux
bytes are not format bytes) — so they are served by the *direct* `FUN_005fd900` callers, of which
`FUN_0067d3e0` is the one that reads the bindings (§2). The file-level *reason* the bake wrote
the table is the clip's binding data (§1); the runtime *consumer* of that table is this path.

## 3 · The case-family taxonomy + coverage (MEASURED)

The sampler dispatch (`FUN_005fd900`, `records.py`'s `DISPATCH`) groups the 48 mapped types into
22 cases; corpus coverage over the 1,461 flag-set files (records / files / records in aux files):

| case family | types | records | files | of which in aux files |
|---|---|---|---|---|
| `case 0` (inline) | 1 | 2,960 | 376 | 141 |
| `caseD_1` | 2, 11 | 1,903 | 301 | 803 |
| `caseD_1c` | 29 | 1,591 | 329 | 1,584 |
| **`caseD_9`** (the decoded family) | 10, 13, 14, 17, 22, 23, 32, 39, 40, 41, 42, 46 | **1,143** | 386 | 347 |
| `case 4` (inline) | 5 | 953 | 121 | 2 |
| `case 0x23` (inline) | 36 | 453 | 453 | 449 |
| `caseD_8` | 9, 38 | 351 | 142 | 182 |
| `caseD_e` | 15, 16, 18, 19, 20, 21, 25 | 325 | 111 | **0** |
| `caseD_19` | 26 | 247 | 67 | **0** |
| `caseD_2a` | 43, 44, 47 | 221 | 53 | 211 |
| `caseD_2` | 3 | 221 | 82 | 75 |
| `caseD_2c` | 45 | 185 | 44 | 16 |
| `caseD_17` | 24 | 183 | 69 | **0** |
| `caseD_6` | 7 | 163 | 116 | 85 |
| `caseD_3` | 4 | 114 | 102 | 19 |
| `caseD_5` | 6, 27, 28 | 48 | 40 | **0** |
| `caseD_b` | 12 | 11 | 8 | **0** |
| `case 0x24` (inline) | 37 | 8 | 8 | 8 |
| `caseD_2f` | 48 | 8 | 7 | 1 |
| `caseD_1d` | 30, 31 | 5 | 5 | 5 |
| `caseD_20` | 33, 34, 35 | 2 | 1 | **0** |
| default (no-op) | 8, ≥49 | 0 | 0 | 0 |

- **The decoded coverage is narrow**: only **40 of 1,461** files (2.7 %) have *all* their records
  in the `caseD_9` family; 1,421 files have at least one record outside it. By records the
  `caseD_9` family is 1,143/11,095 (10.3 %).
- `caseD_9` types present: 10 (82), 13 (198), 14 (105), 17 (142), 22 (12), 23 (171), 32 (142),
  40 (138), 41 (62), 46 (91); **39 and 42 are absent** from the corpus (dispatch-only).
- Families with **zero** aux-file records (`caseD_e`, `caseD_19`, `caseD_17`, `caseD_5`,
  `caseD_b`, `caseD_20`) — the "older/other" per-type bodies; the families that dominate the aux
  population are `caseD_1c` (29), `caseD_1` (2/11), `caseD_9`, `case 0x23` (36).
- The **apply** dispatch (`FUN_0067d8c0`, §2) is a *second* taxonomy with its own coverage:
  48 jump-table entries, with types 8, 32, 36, 37, 38 → the apply default (the sampler's camera
  cases 36/37 have **no** body in this dispatch — their apply is elsewhere, not decoded here).

## 4 · What `records.py` does with each (MEASURED; no decode gap found)

| reader concern | current behaviour | verdict |
|---|---|---|
| flag-set structure parse | `parse_motion` reads `+0x08` into `aux8_off` and validates the record array `[+0x10, +0x10+12·tracks)` | correct for all 1,461 (0 parse errors) |
| record roles | `read_records` reads `{type, p1, p2}` at stride `0xC`; p1/p2 resolved `data_base + (v & 0x7fffffff)` | correct (T2's verify) |
| the aux array | recorded (`aux8_off`) but **not read**; no assertion covers it | **correct for the decode**: the `caseD_9` sampler body (`FUN_00600ef0`) takes no aux; the aux is an *apply-side* input (§2) |
| pointer safety | no flag-set record pointer lands in the aux region or outside the data region (0/11,095) | correct |
| type-13 decode | `decode`/`scan` handle the `caseD_9` family (the sampler's arithmetic) | correct; the aux changes nothing about the sampled values |
| coverage | types outside `caseD_9` are *reported* (`DISPATCH` entry) but not decoded — by design | incomplete for a converter (§5) |

**No mis-parse or incompleteness was proven in `records.py`'s stated scope** (the flag-set record
reader + the sampler's per-type dispatch), so per T8's gated fix path **no code change is made** —
the findings are recorded here and proposed below. The one *substantive* gap the aux exposes is a
**converter-side** one, not a reader one: a port that decodes the sampled values but ignores the
bindings will apply every track to the same node (the tool's `dump` prints `aux8 %#x` only).

## 5 · Verification (what was run; re-derivation)

1. **The counts + layout identities** — `out/re/aux/aux_identities.py` over all 4,455 `.anb`
   (`out/re/aux/aux-identities.txt`): `flag_set 1461, aux 872, noaux 589, errors 0`;
   `aux_at_records_end 872/872`, `struct_after_aux 872/872`, `struct_after_records_noaux 589/589`;
   3,928 aux values (63 distinct) and the per-type vocabularies. Re-derivable standalone:

   ```bash
   export SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
   python3 - <<'PY'
   import glob, os, sys; sys.path.insert(0, "tools/re/anb"); import records
   aux = noaux = flag = 0
   for p in sorted(glob.glob(os.path.join(records.game_root(), "cp3shadow/anim/*.anb"))):
       d = open(p, "rb").read()
       if d[:4] != records.XFF_MAGIC: continue
       m, b = records.parse_motion(d)
       if not m.flag_set: continue
       flag += 1
       if m.aux8_off:
           aux += 1
           assert m.aux8_off == m.records_off + 12 * m.tracks
           assert m.file_off - b == m.aux8_off + 4 * m.tracks
       else:
           noaux += 1
           assert m.file_off - b == m.records_off + 12 * m.tracks
   print("flag-set %d: aux %d / noaux %d" % (flag, aux, noaux))
   PY
   # flag-set 1461: aux 872 / noaux 589
   ```

2. **The case-family coverage** — `out/re/aux/families.py` (`families.txt`): the table of §3; the
   type histogram reproduces T2 §4's exactly (`1:2960 … 48:8`).
3. **`records.py verify` re-run** — **20/20 PASS, exit 0** (4,455 files, 1,461 flag-set, 0 parse
   errors; 198 type-13 tracks decode, 0 failures) — the baseline is green and untouched.
4. **The code legs** — `FUN_005fd860`'s only caller (1 edge), the getter/apply/camera decompiles
   (§2), the apply jump table read raw from `out/re/eboot.elf` at `0x01270df0` (48 entries;
   VA→file `+0x4000`), the `FUN_0067d8c0` switch head (`CMP ECX,0x2f`, disasm `log-t8-2.log`),
   the case-body disassembly (`log-t8-3.log`), and the call-site register mapping at
   `0x0067d774`/`0x0067d853` (disasm, `log-t8-2.log`).
5. **Determinism** — headless runs used `-noanalysis` on a *copy* of the sampler project
   (the source project was never opened); raw-byte reads are the committed stdlib pattern.

## 6 · Open items (INFERRED / unresolved)

- **The writer's exact condition** — "the clip's source carries a node-binding table" is INFERRED
  from the consumers; the *file* only carries the consequence (`+0x08`). The 20 all-`-1` objanim
  files and the 4 v7 noaux cameras are the recorded boundary cases.
- **`FUN_006163a0`** (the general node-id resolution) and **`FUN_00616c20`** (the bit-16
  indirection target) are not decompiled here — the *shape* of the value encoding is measured
  (§2), the resolution internals are not.
- **Node-id space** — whether the ids are per-object node indices (the `buddha_a_death` piece
  ids up to `0x4B` suggest a ~76-node model) or a shared/global space is not settled.
- **The camera ids** (`0x1000x`) — the camera case stores them as the camera id; what the camera
  side indexes with them is not decoded (T2 §4's open item, unchanged).
- **The `caseD_9` family's other types** — 10/14/17/22/23/32/39–42/46 share the sampler body but
  were not decoded end-to-end (T2 §4's dispatch table; the T8 coverage numbers are §3).

## 7 · What this changes

The aux population is now **characterized, not open**: the predicate (`+0x08 != 0`, 872/1,461), the
layout identity (records → aux → structure, 872/872 + 589/589), the *reader* (`FUN_005fd860`, the
single caller, the camera inline reads), the *consumer* (the apply dispatch's node-reference
semantics, with `-1/-2/-3` self-slots and the bit-16 indirection decoded from `FUN_005fc100`), the
value vocabulary (63 distinct values, per-type tables), and the **negative result** that no
file-local predicate reproduces the split (same names/types/versions/formats on both sides). The
case-family coverage is tabulated: the decoded `caseD_9` family covers 1,143/11,095 records
(10.3 %) and **40/1,461 files** entirely; the remaining families are dispatch-known but undecoded,
which is the converter's next mechanical step (per-type apply bodies), not a parse gap. `records.py`
needs no change for the decode it implements; a port needs the bindings (the aux) to place tracks
on nodes.
