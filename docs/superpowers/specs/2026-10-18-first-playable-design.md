# Shadow of the Colossus — first playable: walking around the world

**Design amendment · 2026-10-18**

Amends the 2026-10-16 SDL design in exactly one place — S3's definition (§2 below) —
and records the decisions and recon facts of the 2026-10-18 planning round (§1, §3–5).
Everything else in the 2026-10-16 design (intent, constraints, decisions D-S1…D-S9,
the RE plan, the offline-first contract) is inherited unchanged and remains the
reference.

---

## 1 · The first playable moment (decision — user, 2026-10-18)

The first moment that counts as *playing* is **walking around the world as Wander** —
not the first colossus fight.

- The ladder to it: **S2 "It moves"** (converted `.anb` animation in the app) →
  **S3 "The arena"** (the Valus arena composed from real game data, with a controller
  so *you* steer Wander).
- **S4 "The slice"** (first colossus defeatable) remains the first *encounter*; it is
  no longer the definition of the first playable milestone.
- Nothing else in the ladder changes: S2 still precedes S3; RE-1 and RE-3 still gate
  them respectively.

## 2 · S3 sharpened: "explorable" means you control Wander (user, 2026-10-18)

The 2026-10-16 §6 row reads "Valus arena explorable: already-converted level ground +
props + composition from data (RE-3); ≥60 fps class". **"Explorable" is pinned to: the
player controls Wander** — the arena loads from data and you walk/run in it with the
game-style third-person camera, on real converted geometry. A free camera alone does
not satisfy S3.

Controls and feel (decided with the user):

- **Keyboard + mouse at S3**; the gamepad arrives with S4's combat work (an SDL
  gamepad layer is not part of S3).
- **Camera:** auto-follow like the game, plus drag-to-look nudges — the cursor is
  never captured (standing constraint).
- **Feel:** S3's movement and camera are hand-tuned to feel game-like. The original's
  exact behavior (movement/camera constants) stays S4's RE-4 pull — no binary RE on
  S3's critical path.

## 3 · Recon facts recorded (2026-10-18)

Findings that shaped this cycle's direction — recorded here, not decisions:

- **Wander's clips are `player_*`** — 967 in `cp3shadow/anim/`, state-machine naming
  (`player_a_stand`, run states, attacks, grip/climb moves, riding layers, damage,
  cutscene intros). The horse (`horse_*`, 270 clips) and `minotaur_*` (Valus, 135)
  live in the same bank.
- **The converted glb already carries the rig** — Wander: 1 skin, 348 joints with real
  bone names (Bone_root, Bone_HARA, fringe…), JOINTS_0/WEIGHTS_0 skin weights; no
  animation tracks yet; joints hold bind matrices until animation lands (aqua notes
  §11: switching to TRS is a mechanical substitution).
- **RE-1 lead** — the vendored library already parses BluePoint's `CANI` animation
  container (position/quaternion/scale channels, fps, frame sets, vertex-morph tracks,
  compression handling; community-credited notes). Object animations ship as `.cani`
  files in the tree; the actor bank is `.anb` (`xff` magic `78 66 66 00`, recorded in
  the 2026-10-15 design). The RE-1 spike's first experiment: how close `.anb` sits to
  that parser; Ghidra + the community notes remain the fallback.
- **Reference captures have a workflow** — the dev-only port records/replays pad
  routes (`BB_PAD_RECORD`, F9) and compares screenshots.

## 4 · S2's demo clips (pinned with the user, 2026-10-18)

First "It moves" demo: **Wander's idle (`player_a_stand`)** plus **the horse** as the
second rig (riding is on the road to walking the world).

## 5 · S2's reference capture (decided with the user, 2026-10-18)

S2's done-line is "plays a converted `.anb` clip correctly vs a dev-only reference
capture"; no captures exist yet. The workflow:

- The plan adds a thin **macOS capture script** that drives the user's port rig when
  the Mac is free — it writes only into the port's own `out/` (saves stay untouched).
- It captures **Wander's idle and the horse**; the comparison is side-by-side stills
  at pinned times.
- **The user signs off** on "correctly" — reference-tuned, not frame-exact (D7).

*Personal-use fan project. No game assets are distributed from this repository.*
