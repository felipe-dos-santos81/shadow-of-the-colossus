# S12 RE gates R1–R5 (2026-10-23) — Gaius (`KNIGHT`, `knight_a`)

The S12 design (`docs/superpowers/specs/2026-10-23-s12-gaius-design.md` §13) opens the milestone with five
time-boxed RE gates. Each gate has a named fallback. This note records each gate's evidence and its verdict.
Later tasks take each gate's values from its **Values for later tasks** block.

**Labels.**
- **data**: a shipped table or file (eboot tables, `.anb`/objanim tracks).
- **binary**: a decompile or disassembly of `out/re/eboot.elf`.
- **measured**: computed from the baked glb (FK) or the `.anb` root series.
- **guess**: an inference.

**Addresses.** VAs are eboot VAs; file offset = VA + 0x4000.
- `W` is the boss work block: `(FUN_00746d50()+0x17) & ~0xf`.
- `sub` is the actor sub-object: `FUN_00611510(actor)`.

**Method.**
- Ghidra 12.2 headless, `-readOnly -noanalysis`, on an APFS clone of `out/re/ghidra` (the shared project is
  never locked).
- The S11 `scrdb.py`/`catalog.py` table readers.
- The recon objanim decoders.
- A stdlib glb FK script on Gaius's bake. The bake used the view `out/libviews/s12-gaius`, 62 `knight_a_*` clips,
  byte-sorted, with the `bake-quadratus` invocation pattern.
- Scratch and dumps: `.superpowers/sdd/2026-10-23-s12-gaius/re/` (gitignored).

## Verdicts

```
R1: CLOSED (attack issue rule, selection order, cooldowns, control bits, kind dispatch, goal per kind) / FALLBACK (gait ladder: walk vs fast-turn vs stop-turn selection in FUN_008158d0 keeps the §13.1 30° rule; mode 12's goal is a guess, authored as none) — FUN_0085a960 only arms; the runner is FUN_0085ab40 (kind 10). The ATTAIN reading (goal = bring the player into the attain volume) is a GUESS.
R2: CLOSED — control-def = AND of 0x68-byte records (expect at +0x58); all 28 opcodes in KNIGHT's 67 control-defs decoded; row fires on (ctrl bit requested) OR (condition).
R3: CLOSED — atk1_3 blade window = [9, 75] (fling volume, type 41 on track 43, active f0–8; KNIGHT_PULLSWD event start f75 < stillness end f77/f86).
R4: CLOSED — bone parent = glb joint of the same skb name, offset (−x, y, z) and quat (−x, y, z, w) in that joint's glb frame; entity (−x, y, z); skelton (x, hipY + y, −z). Residuals: slam blade 0.70 m / 0.2° and far end 0.09 m from the tip (chain frame); foot spheres 1.92 m (R) / 0.54 m (L) from the footprint centres, inside r 7.26; footstep spawns 0 frames; Quadratus t46 holds the impact 5.5 m inside (unmirrored: misses by 5.5 m).
R5: CLOSED — player_bearing_in = atan2(local x, local z) in the game frame, + = the boss's RIGHT; app bearing (+ = left) = −game bearing.
```

---

## R1 — the attack data and the locomotion kinds (§13.1)

### What FUN_0085a960 is (binary)

`FUN_0085a960` @0x85a960 (197 B) only **arms** the attack data:
- `W+0x4b78` = the current mode's slowstatus `+0x10` attackdata handle;
- `W+0x4b90` = 0.

Its twin `FUN_0085a890` does the same from the **pending** mode. The walker (`FUN_0082c080`) calls it on
**every** mode switch, so `W+0x4b78` = the new mode's attackdata, or null.

The walker's step-12 call to `FUN_0085a960` needs slowstatus `+0x20` (attack trigger). That field is null for
every KNIGHT row, so for Gaius only the switch arms. The consumer is the per-mode locomotion kind.

### The per-mode locomotion kind (binary)

On a switch, the walker writes these from the mode's `scr_slowstatus`:
- `+00` → `W+0x4730` (`FUN_00859580`);
- `+04` → `W+0x4748`;
- `+08` → `W+0x474d`;
- `+0c` → `W+0x3dc`.

Every frame `FUN_00859ad0` (@0x859ad0) runs, but only when the root kind `W+0x4b30` ≠ −1:

| kind `W+0x4730` | on entry (kind changed) | every frame |
|---|---|---|
| −1 | route indices cleared (`W+0x4764` = −1,−1); `W+0x378` = −1 | `FUN_0085bdb0` |
| 0 | route indices cleared; `W+0x378` = 0 | `FUN_0085bdb0` |
| 1 | route indices cleared; `W+0x378` = 1 | `FUN_0085bdb0` |
| 10 | route indices cleared; `FUN_0081d3a0(W, target)` (follow-target = the player entity) | **`FUN_0085ab40`** (the attack runner) |
| 2–9 | route/waypoint followers | not used by Gaius |

Then `FUN_0081da00` (the path follower) runs for every kind.
- `W+0x378` is its mode: 0 runs the route search (`FUN_00820730`/`FUN_00820940`); 1 is direct. This reading is a
  **guess** from the code shape.
- `arrived` = `W+0x3d6` ∧ `W+0x36c` == 0 (`FUN_0081d5f0`; the same test as the `arrived_goal` predicate).
- The slowleg moves only toward the follower's waypoint 0. Its goal pointer is `FUN_0081d980(W, 0)` =
  `W+0xec` and is non-null only while the waypoint count `W+0x36c` > 0. With no waypoint it emits no move code
  and requests no bit (binary, `FUN_00814180`: `pauStack_268 == 0` skips `FUN_008158d0`/`FUN_008168c0`).

**Where the goal comes from** (binary for the setters; data for which modes run them):
- **Point goal.** `FUN_0081cfc0(W, p)` stores a fixed point in `W+0x54` and clears the follow target `W+0x88`.
  It is called by the graph actions `goal_player` (`FUN_0082e480`: a snapshot of the player position + (0, f, 0)),
  `goal_player_ground` (`FUN_0082e9c0`: the same, snapped to the nav ground) and `goal_lastseen*`.
- **Follow target.** `FUN_0081d3a0(W, entity)` stores a live entity in `W+0x88`. Only kind 10's entry calls it,
  with the player.
- The kind entries for −1/0/1 clear the route indices (`W+0x4764`) and set `W+0x378`. They do **not** clear
  `W+0x54` or `W+0x88` (binary, `FUN_00859ad0`).

**Gaius's modes with kinds 0 and 1** (data: the exported graph's actions):

| mode | kind | goal | per frame |
|---|---|---|---|
| 3 route_patrol (the retry start mode) | 1 | **none.** The mode has no actions. The resident `KNIGHT:F46 → F49` (`goal_lastseen_y(53)`) runs only on `ALWAYS:C28` "kabura-ya is present", which is false in S12. At the retry start no goal has ever been set | `FUN_0085bdb0` (cooldown tick; unarmed, so no attack). The slowleg has no waypoint, so **no movement bits**: he stands until a transition fires (binary + data) |
| 4 lose_sight | 0 | on entry, `ALWAYS:F8`: `goal_player(0)` (a **snapshot** of the player position, not live), `route_replan` (`FUN_0082e920` re-runs `FUN_00859ad0` now), then `goal_player_ground(0)` when `ALWAYS:C107` (not `lost_target`) holds | follower in route-search mode (`W+0x378` = 0) toward the snapshot point; the slowleg's move codes and steer bits until `arrived` |
| 6 lose_sight (point) | 0 | no actions: keeps whatever point goal is stored (the last snapshot) (data; leftover: **guess**) | as mode 4 |
| 12 parent (player attached) | 1 | no goal actions. The leftover is a **guess**: the follow target (the player) set at the mode 9/10 entry, never cleared by kind 1 | unknown: direct follow (`W+0x378` = 1) of the leftover goal |

**Gaius's rows (data, `KNIGHT scr_slowstatus` 0x23367b0).**

| mode | slowstatus row | kind | slowleg enabled (`+60`) | attackdata |
|---|---|---|---|---|
| 0, 1, 2, 8 | 0 | −1 | 0 | — |
| 5, 7 | 1 | −1 | 0 | — |
| 3 route_patrol | 2 | 1 | 1 | — |
| 4, 6 lose_sight | 3 | 0 | 1 (and `+0c` = 1) | — |
| 9 sword_attack | 4 | 10 | 1 | `KNIGHT_SWORD[0]` (rows 0, 1, 2) |
| 10 stab | 5 | 10 | 1 | `KNIGHT_SWORD[4]` (rows 4, 5, 6) |
| 11 counterattack | 6 | −1 | 0 | — |
| 12 parent | 7 | 1 | 1 | — |

**Consequences.**
- No attack control is issued from kinds −1/0/1 in Gaius's graph. `FUN_0085bdb0` acts only when attackdata is
  armed, and only modes 9 and 10 arm it.
- The counterattack's attacks come from the graph's own actions (F27…).

### The attack runner `FUN_0085ab40` (binary)

**Row layout** (`scr_attackdata`, 0x100 B; Gaius's table `KNIGHT_SWORD` @0x2232a98, data 0x2236420).

| offset | field |
|---|---|
| `+08` | chained attackdata |
| `+18` | chain percent (Gaius: 100; no chains) |
| `+20` | allowed control-mode list (null → any) |
| `+30` | "default row" flag |
| `+34` | re-select period, s |
| `+38` | sensing: 0 any, 1 `sensed` `W+0x4aec`, 2 seen/near `W+0x4af0/1` |
| `+40` | condition (null → true) |
| `+50` | FIX volume |
| `+60` | ATK volume |
| `+70`/`+80` | alternative ATK volume and its condition |
| `+90` | ATTAIN volume |
| `+a0`/`+b0` | issue controls |
| `+c0` | the row's motion |
| `+d0`/`+e0`/`+f0` | aim effect (unused by Gaius) |
| `+f4` | cooldown, s |
| `+f8` | issue during cooldown once arrived |

**Volume test.** "Player inside volume V" (`FUN_0065eba0`) tests the player position point against every
judge-volume instance of that name on the boss (`sub+0x398`). For Gaius those are the 15 `_skelton` type-44
defaults (data).

**Per frame:**
1. Add dt to `W+0x4b90` (time since attack) and `W+0x4b8c` (time in row); count the cooldown `W+0x4b94` down
   to 0.
2. For each row i, register motion callbacks on slot i (`FUN_00611530`).
   - onStart (`FUN_006115b0`): `FUN_0085b810`, the chain; Gaius has none.
   - onEnd (`FUN_00611600`, run when the motion retires): `FUN_0085b910`, which does count++, `W+0x4b90` = 0
     and cooldown `W+0x4b94` = row `+f4`.
   - Kinds other than 10 register only onEnd = `FUN_0085bfa0` (the cooldown).
3. If global flag bit 7 (`FUN_0083b0f0` → `DAT_0325f7aa`) is set, clear the row and stop. Meaning: **guess**,
   an event/demo pause; the app should treat it as false.
4. **Select** when there is no row or `W+0x4b8c` > row `+34`, in three passes:
   - (a) The **first row in table order** whose condition and control-mode test hold, whose **FIX** volume
     contains the player and whose sensing holds.
   - (b) Else the first row whose **ATK** volume (or the alternative) contains the player.
   - (c) Else the **first row with `+30` = 1** (the default).
   - If none of these: clear the row, re-request the mode's update control, stop.
   - On a pick, look up its ATTAIN volume. If the row changed, set `W+0x4b8c` = 0. Store the row in `W+0x4b80`.
   - If the attain shape is a cylinder (kind 0), set the **goal** to the attain pose (`FUN_0081cd60` →
     `W+0x3b4..0x3d0`): position, inner = frac·r, r, half-angle (full width/2; a full circle gives π),
     half-height.
5. **Issue.** If the cooldown is > 0, issue only when row `+f8` ≠ 0 **and** he has arrived; else stop.
   - Test the player against the selected row's **ATK** volume. If inside and sensing holds, request the
     row's `+a0` (and `+b0`) control bits.
   - Else, when root kind `W+0x4b30` == 1 (Gaius: 1), run `FUN_0085ba60`. It requests the controls of the
     **first row in table order** whose ATK volume holds the player, with no cooldown check of its own.

**What FIX means.** FIX is a **selection priority**, not an exclusion. §13.1's fallback ("inside ATK and
outside FIX") is contradicted by the binary.

**What ATTAIN means.** The attain volume is in the boss's own frame. "Goal = attain pose" reads as "bring the
player into this volume": slam 30 m ahead r 5, stab 16 m r 4, stomps (±3, 7) r 2. This is a **guess**, but it
fits the measured slam reach (R4).

### The control bits each kind leads to (binary; root kind 1 = the slowleg `FUN_00814180`, every frame)

Gaius's root kind is 1 (data, `KNIGHT scr_data_manager +00` = 1). So every frame
`FUN_00814180(boss, W+0x47c8, W, target)` turns the goal into control bits.

**Slowleg off.** When the mode's slowstatus `+60` = 0, nothing is requested.

**Mode-switch parameters.** These are applied on a switch from slowstatus `+60..+9c`:
- `+64` → `W+0xe0`;
- `+6c`·π/180 → `W+0xe4` (Gaius: 3°);
- `+68` → ctx `+38` (1.0);
- `+78` → ctx `+20`: the MOVE_BACK volume, set on rows 4 and 5;
- `+88` → ctx `+28`: the SLIDE volume, none on Gaius.

**Move codes.** The gait selector `FUN_008158d0` returns a move code. `FUN_008168c0` requests (the
script-ctrl-def bits are read from `PTR_PTR_0222ec38..0222eca0` → `DEFAULT:script-ctrl-def`):

| code | request |
|---|---|
| 0 | `acceleration` (50), only while the control-mode equals the one the slowleg stores at `+0x18` (not identified); plus `dash` (46) if ctx `+30` |
| 1 | `Fast_turn_right` (53) if yaw error ≥ 0, else `High_speed_left_turn` (54) |
| 2 | `Stop_right_turn` (55) if ≥ 0, else `Stop_turn_left` (56) |
| 3, 5 | `Sudden_braking` (57) |
| 4 | `Backward` (52): when the target point is inside the mode's MOVE_BACK volume (rows 4/5), with an undecoded run-time sign test (ctx `+0x158` < 0) |
| 6 | `Right_translation` (58) / `Move_left_parallel` (59) by the lateral sign |

**Steering.** `FUN_008166e0` steers on top of the move code:
- angle > +3° (`DAT_0128d284` = 0.05236) → `Turn_right` (48);
- angle < −3° → `Left_turn` (49);
- it also writes the **virtual stick X** `sub+0xa7c` (`FUN_006da680`): + = right, the turn amount.

**Arrival.** On arrival: |facing error| ≤ `W+0xe4` (3°) → code 3 (brake); else code 2 (turn in place).

**Constants** in the ladder (binary, `0x128d244..`): 5°, 30°, 10°, 60°, 6° (`DAT_0128d244/250/258/25c/260`),
plus 0.8 and 0.5 factors.

**Not decoded.** Which distance/speed branch selects codes 0/1/2 (the slowleg run-time fields `+0x160..+0x1ec`).
That ladder keeps §13.1's rule, extended with the decoded constants.

### Values for later tasks (R1)

- **Arm.** On every mode switch: `armed = mode.slowstatus.attack` (null for all but modes 9/10),
  `timeSinceAttack = 0`. (binary)
- **Cooldown, time in row, time since attack:**
  - cooldown ticks down in every kind (binary);
  - time-in-row ticks only in kind 10 (binary);
  - time-since-attack ticks only in kind 10 and resets on a switch and on each attack motion's end (binary).
- **Kind 10 selection order** (binary): (a) first row whose **FIX** holds the player; (b) else first row whose
  **ATK** holds the player; (c) else the first row with `+30` = 1. Re-select when time-in-row > `+34`.
- **Selection order on Gaius's rows** (data):
  - mode 9 [0 L stomp, 1 R stomp, 2 slam]; mode 10 [4 L stomp, 5 R stomp, 6 stab];
  - default row: 2 (slam) / 6 (stab).
- **Goal.** The selected row's ATTAIN volume (cylinder: r, inner = frac·r, half-angle). (binary; its meaning
  "player inside attain" is a **guess**)
- **Issue rule** (binary):
  - if cooldown > 0 and not (`+f8` ∧ arrived), issue nothing;
  - else, if the player is inside the selected row's ATK: request its control;
  - else request the control of the first row (table order) whose ATK holds the player.
- **Row numbers** (data):

  | row | FIX / ATK / ATTAIN | issue | motion | `+34` | `+f4` | `+f8` |
  |---|---|---|---|---|---|---|
  | 0 | LEG_L_ATK_FIX / LEG_L_ATK / LEG_L_ATK_ATTAIN | `Issue_knight_left_foot_attack` (bit 3) | M18 | 5 s | 15 s | 0 |
  | 1 | LEG_R_… | `Issue_knight_right_foot_attack` (bit 2) | M17 | 5 s | 15 s | 0 |
  | 2 | WEAPON_ATK_FIX / WEAPON_ATK / WEAPON_ATK_ATTAIN | `Issue_knight_sword_attack` (bit 4) | M23 | 15 s | 10 s | 1 |
  | 6 | WEAPON_ATK2_FIX / WEAPON_ATK2 / WEAPON_ATK2_ATTAIN | `Knight_sword_attack_2_issue` (bit 5) | M30 | 15 s | 8 s | 1 |

  - Rows 4/5 = rows 0/1.
  - Every row: sensing 0, condition null, control-mode list null.
- **Default judge volumes** (data, `objanim_knight_a_skelton.anb` t44, parent skelton). All are cylinder
  sectors, half-height 10 (15 for MOVE_BACK/SLIDE); `angle` is the full width:
  - WEAPON_ATK: r 55, inner 0.45, 90°, centre 6 ahead;
  - WEAPON_ATK_FIX: r 70, 180°, 7.5 ahead;
  - WEAPON_ATK2: r 40, inner 0.25, 60°;
  - WEAPON_ATK2_FIX: r 45, 90°;
  - LEG_R/L_ATK: r 23, inner 0.25, 80°, yaw ∓30°;
  - LEG_R/L_ATK_FIX: r 30, 180°, yaw ∓90°;
  - ATTAINs: slam (0, 30) r 5; stab (0, 16) r 4; R stomp (−3, 7) r 2; L stomp (+3, 7) r 2 (glb x, z);
  - MOVE_BACK: r 75, 90°, yawed 180°;
  - SLIDE ×2: r 75, 60°, yaw ±105°.
- **Locomotion** (binary):
  - modes with slowstatus `+60` = 0 (0, 1, 2, 5, 7, 8, 11) request no movement;
  - kinds 0/1/10 request only the bits in the move-code table, plus `Turn_right`/`Left_turn` when
    |steer| > 3°;
  - target inside MOVE_BACK in modes 9/10 (plus the undecoded sign test) → `Backward`.
- **Kinds 0/1, goal and requests per mode** (see the table under "Where the goal comes from"):
  - **mode 3** (kind 1, the retry start mode): no goal, so no movement request; only the cooldown ticks
    (binary + data). §13.1's "kind 1 turns in place" is **not** supported by the binary for mode 3: he stands.
  - **modes 4/6** (kind 0): the goal is a point snapshot of the player, taken at mode 4's entry (binary + data),
    not the live player. §13.1's "kind 0 walks toward the goal, turn controls past ±30°" **stands** as the gait
    choice (FALLBACK below), aimed at that point; `arrived` ends it.
  - **mode 12** (kind 1): the goal is unknown (leftover; **guess**: the player entity). Authored for S12: no
    goal (he stands; the shake-off comes from the mode's actions). Label it authored.
  - The player entity as a live goal (`FUN_0081d3a0`) is set only by kind 10 (modes 9/10).
- **FALLBACK, inside R1:** the selection among codes 0/1/2 (walk / fast turn / stop-turn). Use §13.1's rule:
  turn when |bearing to goal| > 30°, else walk. The arrival brake at |facing| ≤ 3° is binary.

---

## R2 — control-def conditions in the control-assign rows (§13.2)

**Evaluator.** The control-assign walker is `FUN_00631cd0` (@0x631cd0). Its condition evaluator is
`FUN_0062bfd0` (@0x62bfd0, 10.9 KB; a switch over opcodes 2…0xaa).
- Both are called from `FUN_0062f0f0` with the actor's virtual pad `pad = sub+0x9d8`. So `pad+0x134` =
  `sub+0xb0c`, `pad+0x1d8` = `sub+0xbb0`, `pad+0x1f8` = `sub+0xbd0` (the requested-control word).
- A **control-def** is a list of 0x68-byte records ended by opcode 1:

  | offset | field |
  |---|---|
  | `+00` | opcode |
  | `+04` / `+10` | min / max (floats; < −180 or < 0 means unbounded, by opcode family) |
  | `+38` | handle |
  | `+48` | mask |
  | `+50` | value |
  | `+58` | **expect** |

- It is the **AND** of `(result == expect)` over its records. An opcode whose precondition fails yields false
  (it passes only with expect 0). (binary)

**Row firing.** Each control-assign row {control-def, ctrl, orient, next control-mode} **fires** when its ctrl
bit is requested (`sub+0xbd0` | `sub+0xbd8`) **OR** its control-def holds. Rows with no ctrl fire on the
condition alone. (binary)
- Every ctrl Gaius uses is kind 1 (script-ctrl-def `+0xc`), so the veto path for kind 2 never applies (data).
- **All** rows are evaluated each frame. A firing row with an ordinary orient queues `(orient, next mode)`
  through `FUN_00635010`, overwriting the previous queue, so the **last** firing row in table order wins the
  frame. (binary; how the motion layer treats an orient with no edge from the current motion is not decoded)
- The special orient `PTR_DAT_01d8b128` switches the control-mode at once and re-walks the new mode.

**What the AI writes into its virtual pad** (binary):
- stick X `sub+0xa7c` (only writer: `FUN_006da680`, from the slowleg turn);
- mortality bit 9 of `sub+0xb0c` (only writer: `FUN_006da6e0`, from `FUN_006cb1d0` when HP ≤ 0; also when
  `DAT_032553d0` ≤ 0, an unresolved global: **guess**, ignore);
- the hit flag `sub+0xbb0`;
- the request bits `sub+0xbd0`.

Nothing writes stick Y (`sub+0xa78`) or the buttons outside the controller fill. In play no controller is
attached to a boss (**guess**: the button opcodes are the debug pad), so every button, hold and stick-Y opcode
is false for Gaius.

### The 28 opcodes in Gaius's 67 control-defs (binary; `py/cdefs.py` lists every record)

| op | reads | meaning |
|---|---|---|
| 0x36 | `sub+0x9c8 == h38` | current control-mode is X |
| 0x38 | `pad+0x1f8` bit of `h38` | script control X requested this frame |
| 0x54 | `sub+0x260 == h38` | current (base) motion is X |
| 0x56 | `FUN_006357f0(sub, h38) ≥ 0` | motion X active in any layer |
| 0x59 | hit flag ∧ `(hit attr & mask) == value` | landed hit on region (`sub+0x318`; no hit → false) |
| 0x5c | hit flag ∧ hit kind `sub+0x364` | arg 0 → kind ∈ {0,2,4,5}; arg 1 → {1,3}; else kind == arg |
| 0x76 | `pad+0x134` bit 9 | **mortality** (set while HP ≤ 0) |
| 0x42 / 0x43 | `pad+0xa4` / `pad+0xa0` in [min, max] | stick X / stick Y (only X is written by the AI) |
| 0x45 | false only if `sub+0xc60 == self ∧ !pad+0x240` | true for an un-ridden boss (**guess**) |
| 0x3a | `sub+0x528` (mask −1 → true) | ground probe valid (**guess**) |
| 0x64 | `sub+0x70d` | no ground under him (**guess**) |
| 0x3b | `sub+0x529` ∧ `sub+0x6f0` in range | landing-ready (**guess**) |
| 0x28 | `sub+0x1328` ∧ contact value ≤ 60 | ground contact, slope ≤ 60 (**guess**: degrees) |
| 0x27 | `sub+0x1328` ∧ `(contact attr & 0xf8000000) == value` | contact surface class |
| 0x67 0x6c 0x78 0x7a 0x7c 0x7e 0x80 | `pad+0x134` bits 4, 2, 11, 13, 15, 17, 6 | pad buttons/d-pad: false in play |
| 0x7d | `pad+0x136` bit 0 | pad: false |
| 0x69 0x6e 0x71 0x74 0x83 | button down ∧ hold time in [min, max) | pad holds: false in play |

### Values for later tasks (R2) — the name map, now from the binary

*Numbering:* "control-mode N" is `KNIGHT:control-mode[N]` (0 STANDBY … 10 DEATH, the motion layer). "mode N"
elsewhere in this note is `KNIGHT:scr_mode_status[N]` (0 standby … 12 parent, the behaviour graph). They are
different tables.

| condition | rule |
|---|---|
| **hurt rows (17)** `*_DAMAGE_CONDITIONS__SWORD_ONLY_` | landed hit this frame ∧ region ∧ kind |

- **Region:** `(attr & 0x3f0) == value`.
- **Kind, `COMMON_BOSS_CONTROL[10…109]`:** kind ∈ {0,2,4,5} ∧ ¬(base motion == `atk1_start` M23).
- **Kind, `KNIGHT_CONTROL[53…73]`** (belly R/L 0x180/0x170, MOUTH 0x50, hips 0x1e0/0x1f0): kind ∈ {0,5} (sword
  family minus 2 and 4).
- The explosion-arrow / harpoon variants (kind 4 / kind 2) are unreachable for a sword-only player.

| condition | rule |
|---|---|
| `BOSS_MORTALITY_CONDITIONS` (BOSS_CONTROL[44]) | HP ≤ 0 → orient `death` → control-mode 10 (this also satisfies the walker's forced-death gate) |
| `PLAYER_LANDING_DETECTION` (PLAYER_CONTROL[2999]) | ground contact ∧ slope ≤ 60 ∧ ¬(surface class 0x68000000) — use "his root is on the ground" |
| `NO_PLAYER_SCAFFOLDING_DETECTION` (PLAYER_CONTROL[3151]) | probe valid ∧ no ground under him — use "his root has no ground" |
| `KNIGHT_BOSS_MOVEMENT_CONDITIONS` (KNIGHT_CONTROL[0]) | pad hold → false; `acceleration` fires by its bit only |
| `BOSS_*_ROTATION_CONDITIONS` (BOSS_CONTROL[6/10/14/18]) | stick X ≥ +0.3 (right) / ≤ −0.3 (left) ∧ ¬hold ∧ op45; the AI's stick X = its turn amount, so these fire with the `Turn_right`/`Left_turn` bits (write both, or use the bit) |
| `KNIGHT_BOSS_STANDING_RECALLS_CONDITIONS` | stick Y ≤ −0.3 ∧ ¬hold: never true for the AI |
| `GENERAL_PURPOSE_RADIO_CONTROL_UNDER_INPUT_CONDITIONS` | same: never true |
| `KNIGHT_CONTROL[20/23/26]` `*_ATTACK_FAILURE_CONDITIONS` | `Failure_issue_knight_attack` requested ∧ control-mode == 7/8/6 (the S12 failure probe never fires: §13.3) |
| `KNIGHT_CONTROL[41/44/47/50]` arm-raise stop/quit | ¬(`Knight_*_arm_raised_issue` requested) ∧ motion 54/53 active (additive rows: dropped, §13.7) |
| pad-only defs | `COMMON_CONTROL[42/60/64/70/115/123/127/131]`, `KNIGHT_CONTROL[5/8/11/14/17/29/32/35/38]`: false (their rows fire by ctrl bit only) |
| `COMMON_CONTROL[145]` "CONDITIONS_THAT_CAN_NOT_HAVE_A_GENERAL_PURPOSE" | (hit region 0x10) ∧ (hit region 0x20) is impossible, so false; these rows fire by ctrl bit only |

---

## R3 — the blade window in `atk1_3` (§13.4)

The facts behind the window:
- **Start.** `KNIGHT_SWORD_FLING_ATTACK` (a type-41 camera/throw volume on track 43, parent `.skb` record 9) is
  active `atk1_3` **f0–8** (data, `objanim_knight_a_atk1_3.anb`). The start is therefore **f9**.
- **PULLSWD.** The `KNIGHT_PULLSWD` type-29 event on OptBone 12 (`OptBone_sword`) has window **[75, 76]**. It
  fires as the clip crosses f75 (data, t4; `FUN_0060e370` start-crossed).
- **Stillness end** (measured, sword recon `sword.json` stuck_hold on the bake sha256 0490ba82…): the blade
  stays within 0.25 m to `atk1_3` **f77**, within 0.5 m to **f86**; the pull starts f84–86.
- **End** = min(stillness end, PULLSWD) = **75**.
- The knight recon's "pull at about f47" is contradicted by the 0.25 m stillness through f77.
- `atk1_3` is 95 frames at 30 fps (data). `KNIGHT_ATK_SHOCK2` (box 4×0.8×0.5 half extents, 5 damage) is live
  f0–94 over the blade (data). It is not a gate; ruling §13.5 filters it.

### Values for later tasks (R3)

- Band enable: clip `knight_a_atk1_3`, frames **[9, 75]**. Enabled while the frame time is in [9.0, 75.0):
  it opens after the fling volume's last frame (8) and closes when PULLSWD fires at 75.0. 66 frames = 2.2 s
  at 30 fps.

---

## R4 — the damage-volume pose space (§13.6)

**Script.** `re/py/r4.py` and `r4b.py` (FK on `out/libviews/s12-gaius/.../colossus_03_knight.glb`). The chain
frame is the clip-local glb frame plus the `.anb` root x/z, x negated, chained from the chain's first clip.
- The chain root reproduces the sword recon exactly: `atk1_2` f6 → (−0.198, 4.640), recon (−0.198, 4.64).
- The offsets were tested under all 8 sign combinations.

**Parent conventions** (`aux8` word; the exporter's `parent_of`, binary `FUN_005fc100`):
- `0..n` = the `.skb` record → the **glb joint of the same name**. Gaius: record 20 = `Bone_HAND_A_R`. The glb
  joint *index* differs; join by name.
- `−1` = entity.
- `−3` = skelton (hip).
- `0x10000|k` = OptBone k. Gaius has no damage rows on OptBones; his effect rows use k = 12 = `OptBone_sword`,
  the 13th OptBone in glb joint order (**guess**, consistent).

**Results:**

| reference | convention | residual |
|---|---|---|
| `KNIGHT_SWORD_ATK1_DMG` (box ½ 8.158×1.562×3.112, `Bone_HAND_A_R`) at the impact `atk1` f1 vs the measured blade (hilt (−0.43, 7.94, 20.31), tip (−0.08, −0.67, 31.72)) | offset (**−x**, y, z) in the glb joint frame, box axes = joint axes | centre **0.70 m** off the blade line at t = 0.46; long axis **0.2°** off the blade; far end (−0.06, −1.51, **31.63**) vs tip z 31.72 (0.09 m in z) |
| same | offset (+x, y, z) | centre 6.61 m off, at the hilt end (t = 0): rejected |
| `KNIGHT_ATK_SHOCK2` (½ 4×0.8×0.5, offset (9.936, 0, −1.329)) vs the stuck blade, 94 frames (`atk1_2` f6–12, `atk1_3` f0–86) | (**−x**, y, z) | **1.31 m** mean and max (= its own 1.33 m perpendicular offset), axis 0.2°, t 0.69 |
| same | (+x, y, z) | 10.02 m: rejected |
| slam reach 28–30.6 m (the WEAPON_ATK_ATTAIN 30 m ahead) | (−x, y, z) | see **Which frame the reach is in** below: at the impact the box spans z **18.64 … 31.63** in the chain frame and covers 28–30.6; far end − tip = **−0.09 m** in the chain frame and in the root-relative frame alike |
| `KNIGHT_FOOT_DMG` spheres (r 7.258, offset 0) vs the measured touchdowns (footprint AABB centres) | joint position | R (`Bone_TUMASAKI_R`, `atk_leg_r` f9–13): 1.92 m horizontal, y −0.49 (toe tip vs footprint centre). L (`Bone_TOE_L`, `atk_leg_l` f8–12): 0.51–0.57 m. Both footprints lie inside r 7.26 |
| walk footsteps (type-29 `KNIGHT_FOOT_R/L` spawn `knight_walk_dmg`) | event start frame vs FK touchdown | R event f40, toe y reaches 0.00 at **f40**; L event f17, y 0.00 at **f17**: **0 frames** |
| Quadratus's validated t46 (`MAMMOTH_LEG_R_ATK`, entity, raw (18.0875, 2.2019, 37.2787) size (20, 15, 30)) | entity → (**−x**, y, z) | box x [−38.1, 1.9] holds the measured impact (−7.42, 24.15) (5.5 m inside); unmirrored [−1.9, 38.1] misses it by 5.5 m; L_ATK holds none (recon `validate_stomp.py`, re-run) |
| skelton judge volumes | (x, hipY + y, −z) on the raw data (= R(180° about y) ∘ mirror) | WEAPON_ATK_ATTAIN → (0, 0.76, **30.0**): 30 m ahead, matching the slam; LEG_R_ATK_ATTAIN → x −3 (his right, the glb's −x, where `Bone_TUMASAKI_R` stands at x −5.7) |

**Which frame the reach is in** (measured; `re/dumps/r4c.txt`):
- **Source.** "28–30.6 m" comes from the C3 recon (`2026-10-23-s10-s11-acceleration/s12-c3-recon/recon.md:43`,
  `c3.json climb_bands_candidate[0].stuck_pose`), quoted by `knight/behaviour.md:152`. It is the **stuck pose**
  (`atk1_2`'s last frame = `atk1_3` f0): the blade top meets flat ground at z 27.96 and the tip is at z 30.55.
- **Its frame.** The slam chain's start frame, with the root chained through `atk1` (z +4.165) but **without**
  `atk1_2`'s own +0.904 m advance. This is inferred: its stuck-pose hand z 18.55 = the FK clip-local 14.41 +
  4.14 (0.03 m from 4.165).
- **The same span in other frames.**
  - The sword recon's full chain frame (all roots chained, x negated; the frame of its slam sweep z 23.4–31.4):
    28.86–31.45. The sword recon's tip there is 31.48.
  - The root-relative frame (the root at that frame, z 5.069): 23.80–26.39 (the sword recon's tip − root = 26.41).
- **The box against it.** `KNIGHT_SWORD_ATK1_DMG` is active only through `atk1_2` f4, so it is compared at the
  impact (`atk1` f1). There the C3 frame and the chain frame coincide (both chain the root to z 4.165).

  | frame (at `atk1` f1) | box span z | measured tip z | far end − tip |
  |---|---|---|---|
  | chain = C3 | 18.64 … 31.63 | 31.72 | −0.09 m |
  | root-relative | 14.48 … 27.47 | 27.56 | −0.09 m |

  The note's 27.47 m is root-relative, so it is not comparable with "28–30.6". The C3 frame's 28–30.6 lies
  inside the box span. At the box's last active frame (`atk1_2` f4) the far end is 31.43 (chain) vs the tip's
  31.49 at f6.

**What is not determined.** The y/z signs of bone offsets: the combinations differ by ≤ 0.05 m. Keep (y, z) as
stored, consistent with `AnimBridge.ToGlb` (quat (−x, y, z, w)).

### Values for later tasks (R4)

- **Bone parent.** Join the `.skb` record name to the glb joint of that name. The world centre is
  `jointWorld · (−x, y, z)`. The volume rotation is `jointWorldRot · (−qx, qy, qz, qw)`. Box sizes are half
  extents along those axes; the sphere radius is `size.x`. (measured)
- **Entity parent:** the entity frame, (−x, y, z). (measured on Quadratus)
- **Skelton parent:** (x, hipY + y, −z), rotation composed with R(180° about y). (data + measured)
- **Fast rows.** Between `atk1` f0 and f1 the slam box centre moves 3.7 m, (−0.46, 6.96, 26.31) → (−0.27, 3.42,
  25.14). Its far end moves 6.8 m, (0.01, 4.80, 34.17) → (−0.06, −1.51, 31.63). The swept test stays as D6 says.
- **Residuals for the results note:** blade 0.70 m / 0.2° (slam); the slam far end is 0.09 m short of the tip at
  the impact, in the chain frame (= the C3 frame at that instant), and the box covers the C3 28–30.6 m span;
  1.31 m (SHOCK2, by data); feet 1.92 m (R) /
  0.54 m (L) inside r 7.26; footstep spawns 0 frames.

---

## R5 — the sign of `player_bearing_in` (§13.9)

**Decompile and disassembly** of `FUN_00833a70` @0x833a70 (binary):
- It reads the boss quaternion `sub+0x640..0x64c`, the boss position (`FUN_00660fc0(boss)+0x150`) and the
  player position (`W+0x4b18 → +0x150`).
- It forms d = player − boss. A = d·(R00, R01, R02), B = d·(R20, R21, R22), from the quaternion's matrix rows.
- It normalises by the horizontal length and calls `FUN_00586250(A, B)`. That function clamps both and jumps
  to `atan2f(A, B)`.
- It tests `min·k ≤ θ ≤ max·k` with k = `DAT_0128e028` (deg→rad), false without a player.
- So θ = atan2(local x, local z) of the player in the boss frame, **game frame**: + toward the boss's local +x.

**The game's local +x is the boss's right.** The glb negates x, and the glb's +x is the model's left:
measured, Gaius's `Bone_TUMASAKI_R` stands at glb x −5.7.

**Cross-checks:**
- **data, labels.** Valus `Mino_A;_The_goal_is_right` = [20, 180]; `…_is_left` = [−180, −20];
  `right_foot_attack_issuance` = [·, 60]. Gaius `Knight;_counterattack_(right_foot)` C54 = [0, 60];
  `(left_foot)` C57 = [−60, 0].
- **measured, S11/S12 turn data.** In the app convention (+ = left), Gaius's `turn_r` = −89.98° and
  `turn_l` = +90.01° (sword recon). Quadratus's validated S11 code maps app bearing > 0 → `TurnL`
  (`quadratus_fight.cpp:217`, bearing = atan2(dx, dz) − yaw).

### Values for later tasks (R5)

- `gameBearingDeg = −appBearingDeg`, where `appBearingDeg = atan2(dx_local, dz_local)` in the glb/app frame
  (+ = left).
- `player_bearing_in(min, max)` ⇔ `min ≤ −appBearingDeg ≤ max` ⇔ `appBearingDeg ∈ [−max, −min]`.
- **Unit-test name:** `bearing_positive_is_right`. Put the player at glb (−10, 0, 10) relative to a boss facing
  +z: app bearing −45°, game +45°, so C54 (right foot, [0, 60]) holds and C57 does not.
- **Walk row:** with the player on his right (his `TUMASAKI_R` side) he stomps with the right foot.

---

## Reproduce

These commands depend on the gitignored scratch (`.superpowers/sdd/2026-10-23-s12-gaius/re/`: the Ghidra
runner, scripts and dumps) and on the library view. They are not runnable from a fresh clone.

```bash
S=.superpowers/sdd/2026-10-23-s12-gaius/re   # scratch, gitignored
$S/ghidra/run.sh r1c Probe.java dec 0x85ab40 0x85bdb0 0x85b810 0x85b910 0x85ba60   # R1 (also r1a/d/e/k)
$S/ghidra/run.sh r2c Probe.java dec 0x62bfd0 0x635010                              # R2 evaluator
python3 -I $S/py/cdefs.py .superpowers/sdd/2026-10-23-s12-recon/graph/dumps/graph_KNIGHT.json $S/dumps/catalog.txt
$S/ghidra/run.sh r5b Disasm.java 0x833a70 0x833c40                                 # R5
make libview BRANCH=s12-gaius && export SOTC_LIB=$PWD/../../../out/libviews/s12-gaius   # then bake knight_a_* (62)
python3 .superpowers/sdd/2026-10-23-s12-recon/objanim/py/export_events.py knight_a $S/dumps/knight_a.events.json
python3 $S/py/r4.py "$SOTC_LIB/models/shadow/characters/colossus_03_knight/_cmn/colossus_03_knight.glb" \
  .superpowers/sdd/2026-10-23-s12-recon/sword/sword.json $S/dumps/knight_a.events.json      # R4 (+ r4b.py)
```
