# sotc-viewer

The native macOS viewer for the converted *Shadow of the Colossus* asset library:
C++20, CMake, SDL3, SDL_GPU on Metal. It consumes only converted assets
(`out/library`: `.glb`, `.png`, and the audio WAVs + manifest) — never the game tree or
original containers.

**Status (milestone S9): the road to Valus — in interactive play the plain runs on up the arena's
entrance stairs and corridor; the corridor's dead end is a door into Valus's arena and the arena's rim
has one back; Agro waits where she was left; `C` (left shoulder) raises the sword and its light points
to Valus (user walk pending). S8 (Agro), S7 (the return to the shrine) and S6 (the fall) are below it,
unchanged.**
The app opens a resizable window, loads a converted model with its PNGs, draws
it (offscreen target → swapchain → present) at vsync, and frames the scene
bounds on load. A model with a skeleton is skinned on the GPU: the joint palette
is evaluated on the CPU per frame (`worldJoint × inverseBind`) and uploaded as a
vertex storage buffer, blended per vertex by four joint weights. A model whose
glb carries baked animation clips plays its first clip by default; the built-in
`procedural-wave` is only the fallback for a skinned model with no clips.
`--anim` selects a clip and `--anim-time` pins a time for deterministic
captures. In a composed arena (`--manifest`) the player's state drives the loop
instead — still stands, movement walks, Shift runs — with hard clip switches;
`--anim` / `--anim-time` pin a clip manually. A composed scene keeps one palette
and one playback per rig: the player's locomotion and each `entity`'s baked clip
run on independent clocks, and every mesh draws with its own rig's palette.
`--entity-time` pins an entity's clip for captures (clamped into the clip). The
arena's ground lift is skipped
while the active clip carries the root translation series (the series already
holds the ground height), so the feet stay on the heightmap instead of floating
by the model's origin-to-feet offset. A minimal capsule proxy built from the
entity's posed legs and torso stops the player walking through it; `T` toggles a
camera-only framing around the entity's chest (`--camera-target entity` selects
it for captures). Drag to orbit, scroll to zoom, press `F`
to reframe; `Space` plays/pauses, `[`/`]` switch clips in single-model mode (in
arena mode the controller owns the clip), `R` restarts. The window
title shows the fps readout. Escape or the window's close button quits.
`--screenshot` writes the same frame to a PNG.

In a composed scene with an `entity` the encounter is playable: hold `E` at
Valus's body to grab the nearest climb band, climb with `W`/`S` and shimmy with
`A`/`D` (the grip camera follows the attach point), hold `J` to charge and
release to strike. Only the two shipped weakpoints take damage: the forehead
(`weakpoint_03`) and the right upper arm (`weakpoint_04`), each a posed sphere
read from the shell's own vertices — a strike anywhere else clangs, still costs
the swing and still shakes the player off. `W+D` at the top of the chest takes
the branch to the right shoulder, where `W` climbs to the arm. A landed weakpoint
hit damages Valus, plays his body-part reaction and shakes the player off; a
fresh `E` press inside the 0.6 s window catches the nearest band, otherwise the
player lands and staggers. Stamina drains while gripping/climbing/charging and
regenerates on the ground; at zero it forces the fall. The top bar is Valus's
health (shown after his first hit), the amber bar above the stamina bar is
Wander's health, and both are drawn from `renderer.set_hud`. Valus turns toward a
grounded player, stomps when a foot is within 4.5 m and swings the club he now
carries (`c01_minotaur_a_weapon.glb`, attached to `OptBone_sword`) within 14 m;
both attacks have the clip as telegraph and a pinned impact window. Wander takes
35 (stomp) or 45 (club) damage, is invulnerable for 1.0 s after a hit, and dies
at zero: the death clip plays once and freezes on its last frame. Valus dies the
same way and the fight stops reacting. `R` soft-resets the whole encounter —
player health, stamina, i-frames, mode, spawn and camera, and Valus's health,
pose and cooldown — without reloading the scene. A gamepad is mapped the same
way (left stick move, right stick camera, RT grip, west/X/square strike, Start
pause) and hot-plugs without latching an input. `--s41-selftest` and
`--s42-selftest` run the deterministic acceptance timelines described below.

**Valus's death is a sequence, not a freeze.** When his health reaches 0 a phase
machine (`app/src/death.h`) takes over: his fall (`minotaur_a_death_front`,
6.166877 s) plays once from the death clock, the black tendrils erupt from the
two weakpoint shells between 0.4 s and 3.0 s, and the live rig is swapped for the
shipped dead body and its dirtmound at the collapse (6.166877 s). The player's
input is locked through the motion and the collapse — `R`, `M` and `Escape` stay
live — and the control returns at 8.0 s, with the corpse still on the arena
floor and Valus's health bar gone from the HUD. The cinematic camera is
reconstructed: the follow pose, a **12 m close, low angle on the body** (the key is a distance *from Valus*, not a
pull-back from the follow pose — the follow camera sits ≈94 m away, so the 0.4 s key cuts *in*; see
`docs/notes/s6-results.md` §10), a low angle on the tendrils at 3.0 s, a wide on the corpse at 6.2 s,
and the follow pose again at 8.0 s. The tendrils are a **labeled reconstruction**
built from the game's own assets — the `death_tentacle_veins` mask and the three
`.cptx` smoke atlases, 201 stations 0.25 m apart per root — but the `.csdr`
particle simulator is **not** ported, so the motion is authored, not the game's
own curves; the shipped type-36 death-camera clips are undecoded and are not
played either. `--s6-selftest` runs the whole sequence deterministically.

**The return to the shrine (S7).** In interactive play, once the death sequence reaches `Done` (8.0 s)
a transition machine (`app/src/transition.h`) holds on the corpse (2.0 s, control kept), fades to white
(1.0 s, input locked), loads the shrine, fades in (1.5 s) and plays Wander's wake — the baked
`player_a_home_boss01_cut04` + `cut05` clips, in place, 11.669 s — under an authored two-key camera, then
returns the follow camera and the control. The hold, fade and fade colour are **authored** (the binary
holds no timings). The shrine is `out/re/stage/shrine.manifest` (the cathedral glbs, 17 statue instances —
16 idols, slot 1 the broken one plus its skirt — and a posed Mono on the altar, spawn from the
`cathedral_int` start); its ground is a layered heightmap (the floor under the feet, not the roof) with a
coarse 1 m wall mask, and off-screen meshes are culled. The shrine is silent. `R` in the shrine returns to
the arena in its opening state; `R` during the transition resets the arena and re-arms it. `--stage shrine`
starts there directly; `--no-return` keeps S6's end state (the corpse and the control, no return) for the
S6 walk. `--s7-selftest` runs the whole thing deterministically; `--s7-e2e` drives the interactive loop
itself (fixed 1/60 s, scripted input) from one lethal hit through the shrine and `R` back.

**The plain and Agro (S8).** The shrine's front door (an authored trigger box, entry only) fades out
(1.0 s, input locked), loads the plain (`out/re/stage/plain.manifest`: the `levels/home` ground, the
cathedral's exterior as drawn-only `backdrop` lines, Agro as a `horse` record) and fades in (1.5 s); a
box on the shrine steps goes back. The plain's ground refuses a move with no ground, a drop over 1.5 m or
a rise over 1.0 m (the cliff edge is a wall; the arena's entrance stairs are a wall until S9). Controls on
the plain:

| Key (gamepad) | On foot | Mounted |
| --- | --- | --- |
| `Q` (north) | whistle: Agro (within 150 m) trots, or gallops beyond 40 m, to 3 m beside you and neighs | — |
| `E` (south) | mount, within 1.6 m of her side (the side picks the left or right mount) | dismount at a stand or walk, to her mounted side, or the other side when that one has no walkable ground |
| `W` press (stick-forward press) | move | kick: one gait up (walk → trot → gallop) through the start clips; three kicks in 1 s start a 3 s cooldown; a kick near top speed is refused with a neigh |
| `S` (stick back) | move | a tap lowers one gait; held 0.5 s it stops her through the stop clips; at a stand it backs her up |
| `A`/`D` (left stick x) | move | steer (90/60/60/45 °/s at stand/walk/trot/gallop, authored) |
| `C` held (left shoulder) | S9, on the road: raise the sword (the hoist start → loop clips; release lowers it through the end clip). Movement stops; `W`/`A`/`S`/`D` turn him, the camera aims the light; `E`/`Q` are ignored while it is up | at a stand only: the mounted hoist clips; kicks and the dismount are ignored while it is up (release `C` to ride) |
| `R` / `M` / `Escape` | reset to the arena / mute / quit | the same |

Agro's travel is the playing clip's own measured root travel (the loops at 1.31, 5.29 and 14.7 m/s; the
start/stop clips per frame); Wander rides on `OptBone_boy` playing the paired rider clip; every clip
switch on either blends over 0.25 s. The riding camera pulls back per gait (6/7/9 m, authored). Hooves,
the neigh, the whistle, the mount, the "hyah" and the "whoa" are audition picks from `com_b_h`/`com_v`
(the S8 audio slice, 34 entries). The bridle, foliage, jumps, mounted combat and the moving mount are
not in S8. `--stage plain` starts there; `--s8-selftest` and `--s8-e2e` prove it.

S9 (interactive play): the plain is the **road** — the arena's entrance stairs and corridor are walkable,
the corridor's dead end is a door into Valus's arena and the arena has one back, and Agro waits where she
was left. Wander holds `wander_sword.glb` in his right hand on `OptBone_sword` with the data's identity
attachment (the blade along the bone's −Z); the bake lacks the game's additive hoist layer, so a
rotation of 90° × raise about the bone's X stands in for it (authored), and the camera shifts 0.9 m ×
raise to his left so the beam leaves the tip right of centre. Holding `C` raises it; in sunlight its eight-line fan opens around the
camera's look and narrows as the look nears Valus's chest, locking within ≈ 11.5° (`k < 0.01`) — a locked
beam is clipped where its ray first meets the ground or a backdrop (150 m at most) and sparks there. In
shade (a 300 m march toward the sun meets terrain) the light fades out. The shimmer loops while the sword
is up and the search cue sounds once per lock (the S9 audio slice, 36 entries: `make audio` after an S8
acceptance).

The encounter has its own voice. The library's converted audio slice is loaded at start
(a missing manifest or one without a curated cue prints one error naming the manifest or the
event and the scene runs silent; `--s5-selftest` and `--s6-selftest` make the same condition
fatal) and played
through a 32-voice software mixer
into one SDL3 stream at 48 kHz: the music is non-positional, world sounds attenuate with
distance and pan by the player's relative bearing. Music starts on the first landed hit
and loops, and stops at the `R` reset (and on Valus's defeat). `M` mutes and unmutes the whole
mixer; there is no other audio UI. The curated slice is the seven attributed cues
(`player.swing`, `player.hit`, `player.hurt`, `player.death`, `valus.telegraph`,
`valus.impact`, `valus.hurt`) plus `music.boss` (`Boss 1`, 5.1 downmixed to stereo, looped) and,
since S6, `valus.death` (the tendril-burst roar) and `valus.club_swing` (the club whoosh).
The player's fall, ambience, grab/climb and footsteps are
**deferred** — none is in the cue map's `cues` array, so they are silent rather than
placeholders (seven of them are listed under the map's `deferred` array with a reason each).
An **S5-only manifest** (eight entries) still loads and plays in the interactive game: the two
S6-only cues warn once and stay silent when they are reached, and the rest of the mix is
unaffected — `--s6-selftest` demands the ten-entry slice and treats a missing S6 cue as fatal.
`--s5-selftest --audio-mixdown PATH` runs the S4.2
timeline with the audio observer on and writes the proof.

## Build

```bash
cmake -S app -B app/build      # from the repo root
cmake --build app/build -j
```

Requires CMake ≥ 3.25 and SDL3 ≥ 3.4 with SDL3_ttf via Homebrew
(`brew install sdl3 sdl3_ttf`; the HUD uses SDL3_ttf for the stamina label). If
CMake does not find them on its own, configure with
`-DCMAKE_PREFIX_PATH="$(brew --prefix)"`.

## Run

```bash
app/build/sotc-viewer --library out/library
app/build/sotc-viewer --library out/library --model models/objects/props/_cmn/obj_plank_01.glb
app/build/sotc-viewer --library out/library --model models/objects/props/_cmn/obj_plank_01.glb \
  --screenshot out/s1/smoke.png --frames 2
# Wander's baked clips play by default; pin a time for captures:
app/build/sotc-viewer --library out/library \
  --model models/shadow/characters/wander/_cmn/wander.glb \
  --anim player_a_walk --anim-time 0.5 --screenshot out/s2-walk-t05.png
```

The converted library comes from `--library DIR`, else `$SOTC_LIB`; one of the two
is required. Escape or the close button quits.

## Options

| Option | Meaning |
| --- | --- |
| `--library DIR` | converted asset library (default: `$SOTC_LIB`) |
| `--model REL` | model to load, relative to the library; with `--manifest`, the player's glb |
| `--manifest PATH` | compose the scene from a scene manifest; the player is `--model`, the static instances and animated entities come from the file (see [Manifests](#manifests)) |
| `--anim NAME\|IDX` | clip to play, by name or 0-based index (default: the model's first clip; `procedural-wave` only when a skinned model carries no clips); in arena mode it is the manual override of stand/walk/run selection; an unknown name or index exits 2 |
| `--anim-time SEC` | start paused at `SEC` seconds into the clip (wrapped into the clip's duration) — the deterministic-capture flag; it also pins the clip, so arena selection leaves it alone |
| `--entity-time SEC` | pin every manifest `entity`'s clip at `SEC` seconds (clamped into the clip's duration) — the deterministic entity capture; the entity starts paused; needs `--manifest` with an entity |
| `--camera-target player\|entity` | initial framing for captures: `player` (default) is the normal follow camera; `entity` starts in the chest-framing target mode and needs a `--manifest` whose scene has an entity |
| `--screenshot PNG` | render `--frames` frames into the offscreen target, write the PNG, exit |
| `--frames N` | frames the `--screenshot` path renders first (default 3) |
| `--width N` / `--height N` | window size in points (default 1280 × 720) |
| `--selftest` | render one frame, print `s1-status …`, exit |
| `--s4-selftest` | compose the encounter `--manifest`, pin the entity at t=0, assert the spawn is clear and the entity's feet are on the ground, walk the player into the nearest leg capsule until the collision stops it, print the `s4-status`/`s4-spawn`/`s4-entity`/`s4-walk` lines, exit; saves the final frame with `--screenshot`; needs `--manifest` (with an entity) and `--model` |
| `--s41-selftest` | compose the encounter `--manifest` and run the deterministic climb/fight timeline through fixed 1/60 s steps with no window input: S4's approach, grab, 240-frame climb, full-charge head strike, catch, missed catch, forced stamina fall, killing blow, and the post-kill settle to a landed player; prints the `s41-*` lines and `s41-status`, exits 1 on a failed invariant; saves the final frame with `--screenshot`; needs `--manifest` (with an entity) and `--model` |
| `--s41-stills DIR` | with `--s41-selftest`, write `grab.png`, `mid-climb.png`, `strike.png` and `reaction.png` into `DIR`, plus the defeat still (to `--screenshot`, else `DIR/defeat.png`) and the pixel controls under `DIR/control/`; both `DIR` and `DIR/control/` must exist (the acceptance script creates them) |
| `--s42-selftest` | compose the encounter `--manifest` and run the deterministic S4.2 fight timeline through fixed 1/60 s steps with no window input: both shell proxies, the foot/head damage gate, the chest→shoulder→arm route, a stomp and a club hit (with the i-frame gate), the 4 m club miss and the 3 m stomp shockwave, a lethal club with the pinned death clip, the R soft reset, and the teleport-free foot→head stamina climb; prints the `s42-*` lines and `s42-status`, exits 1 on a failed invariant; needs `--manifest` (with an entity) and `--model` |
| `--s42-stills DIR` | with `--s42-selftest`, write six HUD-bearing stills into `DIR`: `stomp-telegraph.png`, `stomp-impact.png`, `club-swing.png`, `hurt.png`, `death.png`, `arm-weakpoint.png`; each capture restores the playback clocks, so the `s42-*` event lines are identical with and without stills; `DIR` must exist (the acceptance script creates it) |
| `--s5-selftest` | compose the encounter `--manifest` and run the S4.2 timeline with the S5 audio observer on: the same fixed 1/60 s steps, one `s5-event t= event= clip=<manifest name> gain=` line per played cue, then the `s5-mix` quality line and `s5-status` last; writes the mixdown to `--audio-mixdown` and exits 1 on a missing cue clip or a silent capture; needs `--manifest` (with an entity) and `--audio-mixdown`; cannot be combined with another selftest |
| `--audio-mixdown PATH` | with `--s5-selftest`, the 48 kHz float stereo WAV the mixer's disk sink writes — the headless acceptance capture; needs `--s5-selftest` |
| `--s6-selftest` | compose the encounter `--manifest` and run the S6 death timeline through fixed 1/60 s steps with no window input: a club swing (so the whoosh fires on the production path), a scripted lethal event through the production `apply_combat_events`, the death phase machine to `Done` (the motion, the tendril burst, the collapse into the corpse, the cinematic camera), the `s6-perf` line and the R soft reset; prints the `s6-phase`, `s6-event`, `s6-perf`, `s6-death`, `s6-status` and `s6-reset` lines (13 in all) and exits 1 on a failed invariant (a missing cue clip, a dead colossus that is still collidable, a half-restored reset); needs `--manifest` (with an entity) and `--model`; cannot be combined with another selftest |
| `--s6-stills DIR` | with `--s6-selftest`, write `motion.png` (0.200 s), `camera.png` (0.400 s, the pull-back key), `tendrils.png` (1.200 s, the burst) and `corpse.png` (6.200 s, the corpse wide) into `DIR`, plus `tendrils-control.png` — the burst's frame with the effect pass suppressed, the control the pixel guard differences against; `DIR` must exist (the acceptance script creates it) |
| `--stage arena\|shrine\|plain` | the first stage (default `arena`); `shrine` and `plain` start there with control, using `--shrine-manifest` / `--plain-manifest`; an interactive start only (needs `--manifest` for the player's frame) |
| `--shrine-manifest PATH` | the shrine's scene manifest (default `out/re/stage/shrine.manifest`, composed by `tools/re/stage/probe.py compose --stage shrine`) |
| `--plain-manifest PATH` | the plain's scene manifest (default `out/re/stage/plain.manifest`, composed by `tools/re/stage/probe.py compose --stage plain`); `--s8-selftest`/`--s8-e2e` load it |
| `--road-manifest PATH` | the road's scene manifest: the Plain stage in interactive play and the S9 runs (default `out/re/stage/road.manifest`, composed by `tools/re/stage/probe.py compose --stage road`) |
| `--no-return` | interactive play keeps S6's end state: no hold/fade/shrine after the death sequence |
| `--s7-selftest` | seed Valus dead and run the return (hold, fade, shrine load, wake, `Free`), the shrine's ground/wall/idol/Mono probes and a 240-frame perf run, then `R` back to the arena and a second shrine load; prints the `s7-phase`/`s7-load`/`s7-ground`/`s7-wall`/`s7-idols`/`s7-mono`/`s7-perf`/`s7-status`/`s7-reset` lines (25 in all); needs `--manifest` (the encounter) and `--model`; cannot be combined with another selftest |
| `--s7-e2e` | the end-to-end proof: the **interactive** loop at a fixed 1/60 s with scripted input (keyboard and gamepad ignored) — one lethal hit through the production combat path after 1 s, then the death, the return, the real stage switch, the wake and `Free`; `R` (the key's handler) 1 s into `Free`, exit 0 1 s into the reloaded arena; prints the `s7e2e-death`/`-return`/`-load`/`-input`/`-wake`/`-status`/`-reset` lines (20) and `s1-status`; needs `--manifest` and `--model`; runs alone (no selftest, capture, `--stage shrine` or `--no-return`) |
| `--s7-stills DIR` | with `--s7-selftest`, write `fade.png` (the arena at fade 0.6), `wake.png`, `altar.png`, `idols.png` and `hall.png` into `DIR` |
| `--s8-selftest` | start on the plain and run the ride through fixed 1/60 s steps with no window input: the whistle from the probe, the left mount, kicks through walk/trot/gallop (each loop's speed over 2 s), a full right steer at the gallop, the held-`S` stop, the back-up, a ridden and an on-foot walk into the cliff edge, the right dismount, the audio counters, a 240-frame perf run, then `R` while galloping; prints the 16 `s8-*` lines and `s1-status`, exits 1 on a failed invariant; needs `--manifest` (the encounter) and `--model`; cannot be combined with another selftest |
| `--s8-stills DIR` | with `--s8-selftest`, write `whistle.png`, `mount.png`, `gallop.png` and `plain.png` into `DIR` |
| `--s8-e2e` | with `--stage shrine`: the **interactive** loop at a fixed 1/60 s with scripted keys (keyboard and gamepad ignored) — walk into the shrine door, whistle, mount, kick to the gallop for 5 s, stop, dismount, walk into the shrine steps, `R` in the shrine; prints `s8e2e-door` (2), `s8e2e-ride`, `s8e2e-reset` and `s1-status`; needs `--manifest` and `--model`; runs alone |
| `--s9-selftest` | start on the road and run fixed 1/60 s steps with no window input: raise the sword at the sunlit probe facing away (the fan), turn the camera to dot 0.80 (the partial) and onto Valus's chest (the lock, the sparks), the shade probe, the mounted raise at a stand with a kick ignored, the walk up the entrance stairs, the corridor door to the arena and the arena door back (Agro where she was left), a 240-frame perf run, then `R` while raised with a carried horse; prints the 15 `s9-*` lines and `s1-status`, exits 1 on a failed invariant; needs `--manifest` (the encounter) and `--model`; cannot be combined with another selftest or e2e |
| `--s9-stills DIR` | with `--s9-selftest`, write `fan.png`, `locked.png`, `entrance.png` and `arena_door.png` into `DIR` |
| `--s9-e2e` | with `--stage shrine`: the **interactive** loop at a fixed 1/60 s with scripted input — the shrine door, whistle, mount, gallop the route to the stairs, rein in, raise and lock, up the stairs, dismount, the corridor door, the production lethal hit, the death, the return to the shrine; prints `s9e2e-door` (2), `-mount`, `-ride`, `-lock`, `-stairs`, `-death`, `-return`, `-done` and `s1-status`; needs `--manifest` and `--model`; refused (exit 2) with a selftest, another e2e, `--screenshot`, `--entity-time`, `--no-return` or without `--stage shrine` |

## Manifests

`--manifest` composes a scene from a line-oriented text file: the static arena
instances, the player (`--model`), and — in S4.0 — one placed, animated entity
(the encounter's Valus). The format accepts more, but framing targets the first
entity and the collision capsule table is Valus's bone names and radii for
every rig, so multiple entities are not a shipping configuration.
The committed schema is `docs/notes/scene-manifest.md`; the directives are:

```text
instance <glbRelPath> <px> <py> <pz> <qx> <qy> <qz> <qw> <sx> <sy> <sz>
entity   <glbRelPath> <px> <py> <pz> <qx> <qy> <qz> <qw> <sx> <sy> <sz> <clipName>
spawn    <px> <py> <pz> <yawDeg>
```

`entity` is S4's addition: the ten numbers are the same placement as an
`instance`'s, and `<clipName>` names the exact clip the entity's own rig loops —
a model that does not carry it (or carries no clips) is a load error naming the
glb and the clip, never a silent rest pose. The player is rig 0; entities are
rigs 1, 2, …; each keeps its own skeleton, palette, and playback clock. The
ground heightmap is built from the static instances only, never from the player
or an entity. The encounter scene (gitignored, composed by
`tools/re/stage/probe.py compose-encounter`) is
`out/re/stage/encounter.manifest`:

```bash
app/build/sotc-viewer --library out/library --manifest out/re/stage/encounter.manifest \
  --model models/shadow/characters/wander/_cmn/wander.glb
```

Controls (no mouse capture: the cursor stays visible and free):

| Gesture | Action |
| --- | --- |
| `W` / `A` / `S` / `D` | Move (camera-relative); Shift runs. While gripping: `W`/`S` climb up/down the band, `A`/`D` shimmy around it; `W+D` at the chest top takes the branch to the right shoulder |
| `E` | Grab (held): attach to the nearest climb band, release to drop. While falling inside the 0.6 s catch window, a fresh press catches the nearest band |
| `J` | Strike (held): charge over 0.9 s, release to plunge. One damage event per swing; a landed hit shakes the player off |
| Left-drag | Orbit: yaw freely, pitch stops at ±89°; in entity target mode, orbit the entity's chest (pitch ±80°); while gripping, orbit the climb camera (pitch ±45°) |
| Scroll | Zoom: distance clamped to [0.5, 500]; in entity target mode, [22, 30] m; while gripping, [5, 8] m |
| `F` | Reframe the scene bounds (single model) / recenter behind the player (manifest mode) / re-seed the climb orbit outward at 6 m (while gripping) |
| `T` | Toggle entity target framing: orbit the entity's chest; `T` again restores the follow view without a snap. Inert when the manifest has no entity (the S3 arena) |
| `Space` | Play/pause the current clip |
| `[` / `]` | Previous / next clip (restarts the clip at t = 0); single-model mode — in arena mode the player's state owns the clip (use `--anim` to pin one) |
| `R` | Soft-reset the encounter (player HP/stamina/i-frames/mode/spawn/camera, Valus HP/pose/cooldown, and the music); without an encounter, restart the current clip |
| `M` | Mute/unmute the whole mixer (music and SFX); the only audio UI |
| Escape / close button | Quit |

Gamepad (standard SDL3 mapping, polled once per frame, hot-plugged):

| Gesture | Action |
| --- | --- |
| Left stick | Move (camera-relative); while gripping, up/down the band and lateral shimmy |
| Right stick | Turn the camera (same drag channel as the mouse) |
| RT (right trigger, past half) | Grab, held |
| West / X / square (west face button) | Strike, held to charge |
| Left shoulder (LB / L1) | S9, on the road: raise the sword, held (the keyboard's `C`) |
| Start | Play/pause the current clip |

A removed gamepad closes its handle and clears every input it owned, so an
unplug mid-grip reads as a released `E` (the grip releases cleanly) rather than a
latched one.

A model whose glb carries baked clips starts playing the model's first clip
(`player_a_stand` on the converted Wander). The `procedural-wave` stand-in (a
2-second deterministic wobble of the first joints) is used only when a skinned
model carries no clips at all — so the skinning path stays demonstrable for
un-animated rigs. Without `--anim-time` the captured time depends on the
frames' wall-clock spacing. The run reports the animation state at startup and
on every clip change:

```text
s2-anim clip=player_a_stand joints=348 procedural=0 t=0.500
```

(`joints` is the skin's joint count — the palette size; `procedural=0` marks a
real baked clip, `procedural=1` the built-in fallback. A model without a
skeleton and without clips prints no such line. A composed scene prints one
line per rig: the player's as above, then one per entity — e.g.
`s2-anim rig=1 clip=minotaur_a_stand joints=238 procedural=0 t=0.000`.)

A screenshot run also prints its size and a per-channel pixel summary, e.g.

```text
s1-pixels min=15,18,0 max=177,180,188 mean=28.1,28.3,27.1
s1-screenshot path=out/s1-smoke.png width=2560 height=1440
```

`--s4-selftest` reports the encounter in the same style. It prints the `s4-spawn`, `s4-entity`,
and `s4-walk` diagnostics first and the `s4-status` line last:

```text
s4-status instances=30 rigs=2 entity=minotaur_a_stand placement=builder-pick spawn=data-position+builder-yaw feet=ok entity_bbox=1123,237,1485,751 pos=378.187,28.430,369.028 frames=423
```

The acceptance script (`scripts/s4-acceptance.sh`) parses these lines;
`docs/notes/s4-results.md` records the measured values and the walk checklist.

`--s41-selftest` is the deterministic S4.1 timeline. It reuses the S4 approach,
walks the last metres to the foot band, grabs, climbs for 240 fixed frames
(asserting a ≥ 3.0 m monotonic rise), strikes the forehead weakpoint with a full
charge, catches the shake-off, deliberately misses the next catch, forces the
stamina pool to zero while attached, lands the killing blow, and settles the
killing blow's fall to a grounded player. The selftest's climb proves a 4.77 m
rise from the foot band to the shin; the strike phases use the ruled test
scaffolding that places the player on the head band's surface, and the
post-landing re-grab is placed on the nearest band's surface (the same
scaffolding class). The interactive walk (`E` then `W`) is the full route, by
hand; the user's walk is the judge. The event lines are:

```text
s41-grab frames=522 band=left_foot pos=380.454,28.425,369.175
s41-climb frames=240 from_y=28.425 to_y=33.199 band=left_shin
s41-strike player_px=1147,661,273,93
s41-strike swing=1 damage=34 health=100->66 weakpoint=0
s41-strike swing=2 damage=20 health=66->46 weakpoint=0
s41-catch band=head
s41-fall frames=102 landed=1
s41-stamina fall=1
s41-strike swing=3 damage=34 health=34->0 weakpoint=0
s41-death clip=minotaur_a_death_front defeated=1
s41-settle frames=177 grounded=1
s41-status grab=left_foot climb=4.77 strikes=3 entity_health=0 mode=grounded defeated=1
```

Every failed invariant exits 1 with a `sotc-viewer: s41-selftest: …` stderr
message naming the phase and the measured value (a climb that rose too little, a
strike that dealt the wrong damage, a death clip that did not freeze, …).

`--s42-selftest` is the deterministic S4.2 timeline. It runs from the pinned stand
pose through both shell proxies, a foot clang and a head strike (34), the
chest→shoulder→arm route to the right-arm weakpoint (34), a stomp (35) with the
i-frame gate, the range split (a club tip 4 m from the target deals 0, a stomp
3 m from the foot deals the 15 shockwave), a club hit (45), a lethal club with
the pinned player death clip, the full R soft reset, and finally the real
teleport-free foot→head climb on the pinned stamina drains. The event lines,
verbatim:

```text
s42-proxy weakpoint=0 radius=1.955
s42-proxy weakpoint=1 radius=2.165
s42-gate foot=0 head=34
s42-arm band=right_arm weakpoint=1 damage=34
s42-stomp damage=35 health=100->65 mode=hurt
s42-miss club=0 stomp=15
s42-club damage=45 health=65->20
s42-death clip=player_a_fall_damage_to_death mode=dead
s42-reset health=100 stamina=100 mode=grounded valus=100
s42-stamina from_y=28.425 to_y=43.484 remaining=39
s42-status proxy=2 gate_foot=0 gate_head=34 arm_band=right_arm arm_weakpoint=1 arm_damage=34 stomp_damage=35 stomp_health=65 club_damage=45 club_health=20 death_clip=player_a_fall_damage_to_death reset_health=100 reset_stamina=100 reset_mode=grounded reset_valus=100 stamina_remaining=39 defeated=0
```

The full foot→head climb gains **15.059 m** and leaves **39** of the 100-point
pool — the measured margin at the sigil, not an assertion against tuning. With
`--s42-stills DIR` the run also writes six HUD-bearing captures
(`stomp-telegraph`, `stomp-impact`, `club-swing`, `hurt`, `death`,
`arm-weakpoint`); each capture snapshots and restores the playback clocks, so
the timeline's `s42-*` values are byte-identical with and without stills. The
acceptance script (`scripts/s42-acceptance.sh`) parses every line, guards the
stills, measures the marginal frame cost and runs the S4.1 acceptance unchanged;
`docs/notes/s42-results.md` records the measured values and the walk checklist.

`--s5-selftest` is the same S4.2 timeline with the S5 audio observer on and a
disk-sink capture. It prints one `s5-event` line per played cue, in order,
each naming the manifest entry (not `bank/name`), then the mixdown's quality
line and `s5-status` last:

```text
s5-event t=1.017 event=strike clip=player.swing gain=0.60
s5-event t=2.050 event=strike clip=player.swing gain=0.60
s5-event t=2.050 event=strike_hit clip=player.hit gain=0.60
s5-event t=2.050 event=valus_reaction clip=valus.hurt gain=0.60
s5-event t=2.050 event=music_start clip=music.boss gain=0.45
s5-event t=13.667 event=strike clip=player.swing gain=0.60
s5-event t=13.667 event=strike_hit clip=player.hit gain=0.60
s5-event t=13.667 event=valus_reaction clip=valus.hurt gain=0.60
s5-event t=15.700 event=stomp_start clip=valus.telegraph gain=0.60
s5-event t=16.233 event=stomp_impact clip=valus.impact gain=0.50
s5-event t=16.233 event=hurt_light clip=player.hurt gain=1.00
s5-event t=17.250 event=club_start clip=valus.telegraph gain=0.60
s5-event t=17.683 event=club_impact clip=valus.impact gain=0.50
s5-event t=17.683 event=hurt_heavy clip=player.hurt gain=1.25
s5-event t=18.933 event=club_start clip=valus.telegraph gain=0.60
s5-event t=19.367 event=club_impact clip=valus.impact gain=0.50
s5-event t=19.367 event=death clip=player.death gain=0.80
s5-event t=20.483 event=music_stop clip=music.boss gain=0.00
s5-event t=44.566 event=music_start clip=music.boss gain=0.45
s5-event t=44.566 event=music_stop clip=music.boss gain=0.00
s5-mix frames=2140000 rms=0.0514 peak=0.9022 window_rms=0.1520
s5-status mixdown=…/out/s5/mix.wav events=20 music=1 defeat_stop=1
```

The first strike (1.017 s) is the foot clang: it swings but deals no damage, so
it plays no hit and starts no music. The first landed hit starts Boss 1;
`hurt_heavy` is the same `player.hurt` clip pushed to 1.25; the music stops at
the reset (20.483 s), **1.116 s after the death at 19.367 s**, because the reset is what ends
the fight in this timeline. The last two lines are a **synthetic post-reset defeat phase**
(R18): the score starts again and one step with `encounter.defeated` set runs the app's own
`if (encounter.defeated) audio.stop_music()`, so the stop wiring is proven here. The real
weakpoint defeat is the S4.1 acceptance (audio off) plus the user's walk. With audio off the
timeline prints the same `s42-*` lines byte for byte.

`--s6-selftest` is the S6 death timeline: a club swing on the production path (so the whoosh
fires), the scripted lethal event through the production `apply_combat_events`, and then the
death machine to `Done` with the cinematic camera, the tendril burst, the corpse swap and the R
soft reset. The event lines, verbatim:

```text
s6-phase phase=Idle t=0.000 total=0.000 lock=0 corpse=0 roots=0
s6-event t=0.000 event=club_start clip=valus.telegraph gain=0.60
s6-event t=0.300 event=club_swing clip=valus.club_swing gain=0.70
s6-event t=0.000 event=music_start clip=music.boss gain=0.45
s6-event t=0.000 event=music_stop clip=music.boss gain=0.00
s6-phase phase=Motion t=0.000 total=0.000 lock=1 corpse=0 roots=2
s6-event t=0.400 event=valus_defeat clip=valus.death gain=0.80
s6-phase phase=Collapse t=0.016 total=6.183 lock=1 corpse=1 roots=2
s6-phase phase=Done t=0.000 total=8.000 lock=0 corpse=1 roots=2
s6-death clip=minotaur_a_death_front total=8.000 corpse=1 health=0 camera=follow
s6-status defeated=1 corpse=1 roots=2 player_dead=0
s6-reset phase=Idle corpse=0 lock=0 valus=100 player=100
```

The score's start and its stop are the same step: the defeat is the downbeat and the
defeat stops the score. The death roar fires at 0.400 s, when the tendril burst
begins. `Collapse` lands on the death clip's own end (6.166877 s), and `Done` at the
8.0 s cinematic end, with the input unlocked and the corpse still on the floor.

With `--s6-stills DIR` the run also writes the four stills and the control frame
(`tendrils-control.png`, the burst's frame with the effect pass suppressed) and prints the
effect pass's price:

```text
s6-perf frames=240 effect_ms=2185.257 base_ms=2145.842 marginal_ms=0.164 frame_ms=9.105
```

The acceptance script (`scripts/s6-acceptance.sh`) parses every line, differences the burst
against its control, guards the stills, checks the frame cost and runs the frozen S5
acceptance unchanged; `docs/notes/s6-results.md` records the measured values and the walk
checklist.

A manifest with an `entity` is an encounter: the player's combat clips
(`player_a_grip_loop`, `player_a_grip_move_f1`,
`player_a_grip_float_u_hi_attack`, `player_a_damage_m_fly2`,
`player_a_damage_h_fly1`, `player_a_damage_h_fly_end`,
`player_a_fall_damage_to_death`) and the entity's reaction, attack and death
clips (`minotaur_a_add_head_damage`, `minotaur_a_add_ude_r_damage`,
`minotaur_a_add_mune_r_damage`, `minotaur_a_atk_leg_l`,
`minotaur_a_atk_leg_r`, `minotaur_a_atk_weapon2_a`, `minotaur_a_death_front`)
must all be in the loaded glbs; the club (`c01_minotaur_a_weapon.glb`) must be in
the library and `OptBone_sword` in the entity rig. A missing clip (or a missing
club glb) is a load error (exit 2) naming the glb and the clip — never a silent
fallback to the first clip. A rig without `OptBone_sword` degrades instead: one
line on stderr and no club drawn, so the fight still loads without its prop. The
S3 arena has no entity and needs none of them.

## Audio

The viewer plays only converted audio. `bp-convert audio` decodes the curated cues from the
game's resident `SXDF` banks and the `music.sxd1`/`music.sxd2` pair at **build time** into
`<library>/audio/<bank>/<event>.wav` (48 kHz float PCM) plus
`<library>/audio/audio.manifest`; the runtime never parses `.sxd`/`.sxd2`. A manifest that lacks
a cue the table names is a **fatal load error naming the event** in `--s5-selftest` and
`--s6-selftest`; interactive
play warns once and runs the scene **silent** (the whole mixer is disabled, never a partial mix
or a placeholder).

```bash
export SOTC_GAME_ROOT=…/SOTC-extract/extracted/game
dotnet exec out/bin/bp-convert.dll audio --root "$SOTC_GAME_ROOT" --out out/library \
  --map tools/re/audio/s5_map.json --force
# -> manifest: out/library/audio/audio.manifest (8 entries)
# -> audio: 8 written, 0 reused, 8 manifest entries
dotnet exec out/bin/bp-convert.dll audio --root "$SOTC_GAME_ROOT" --out out/library \
  --map tools/re/audio/s6_map.json --force
# -> the same manifest with 10 entries (the eight above + valus.death + valus.club_swing)
```

`tools/re/audio/s5_map.json` is the slice's authority: seven attributed cues (`player.swing`,
`player.hit`, `player.hurt`, `player.death`, `valus.telegraph`, `valus.impact`,
`valus.hurt`) and `music.boss`. Only `valus.impact` is chain-verified (`NAME` → `REQD` →
`SEQD` → `PLAY_TONE` → `WAVE`); the other six are audition picks, and every 0x21 PS-ADPCM
row is a fitted-filter decode. Each row's method, identity confidence and fidelity are in
`docs/notes/s5-audio-attribution.md`; the decode itself is in
`docs/notes/s5-audio-recon.md`. `tools/re/audio/s6_map.json` adds `valus.death` (the roar that
fires with the tendril burst) and `valus.club_swing` (the whoosh that fires with the club
swing) — both audition rows with a high identity confidence and a medium fidelity; the S5 map
and the S5 slice stay frozen. **Deferred**: none of the player's fall, ambience, grab/climb or
footsteps is in either map's `cues` array, so they are silent rather than replaced. Seven of
them are listed under the maps' `deferred` array with a reason each (`valus.grab`,
`valus.fall`, `valus.breath`, `valus.ambience`, `player.grab`, `player.fall`, `player.breath`);
the footsteps have no entry. The user's ear is the identity check (`docs/notes/s5-results.md`,
`docs/notes/s6-results.md`), and `valus.impact` (the
five-layer ground hit, 8.15 s) rings past the impact window — the checklist asks whether that
tail reads as an impact or a rumble.

The mixer is a 32-voice software mix at 48 kHz float stereo into one SDL3 stream: category
gains, distance attenuation and bearing pan for world sounds, whole-track loops for music,
oldest/quietest voice stealing, a 0.85 master headroom before the clamp, and `M` to mute.
`--s5-selftest --audio-mixdown out/s5/mix.wav` proves it headlessly (no audio device); the
acceptance script (`scripts/s5-acceptance.sh`) runs it, checks the sequence / the WAV /
`ffmpeg astats`, proves the `s42-*` lines are unchanged with audio off, then runs the S4.2
acceptance unchanged. `scripts/s6-acceptance.sh` runs that script first and then decodes the
S6 slice's ten entries.

## Known gaps (S6)

- **The tendrils are a labeled reconstruction.** The textures, the emitter roles and the
  201 × 0.25 m ribbon geometry are the game's own; the `.csdr` GCN particle simulator is
  **not** ported, so the motion is the builder's authored curl and turbulence, not the
  game's curves. There is no per-segment stagger either — one growth front.
- **The death camera is authored.** The shipped `camera_minoa_death_front_camera_01/02.anb`
  clips are type 36 (undecoded) and are not played; the five-key path is reconstructed.
- **No ragdoll.** The corpse is the converted dead body plus the dirtmound, placed at the
  entity's transform — static geometry, not a physics body.
- **The vein mask is anisotropic.** The ribbon samples the `death_tentacle_veins` mask with
  `u` across the width and `v` along the path (about 100:1), so the veins read as fine
  cross-ribs rather than the game's own stretch.
- **The effect's vertex buffer is not cycled.** One persistent dynamic buffer plus a fence wait
  per copy, where the design asked for a ring; the measured cost (≈0.1 ms/frame) says it does
  not matter yet, so it is recorded rather than paid for.
- **The burst is subtle in the wide framing.** The scripted still measures the effect's
  footprint at 14 599 changed pixels against its control; the shape and the colour are the
  user's eye check, not the script's.

## Tests

```bash
export SOTC_LIB=…/out/library   # repo convention: app tests fail fast without it
ctest --test-dir app/build --output-on-failure
```

- `unit_tests` — doctest cases (asset IO, camera math, quaternion/pose math,
  animation sampling).
- `windowed_selftest` — opens the window, compiles the shader, renders one
  frame, and asserts the `s1-status windowed=1 relative_mouse=0 mouse_auto_capture=0`
  line. `mesh.metal` must sit next to the binary (the build copies it; a bad
  shader fails this test loudly). Run it with `--model` for a skinned model to
  exercise the playback path as well:
  `app/build/sotc-viewer --library "$SOTC_LIB" --model models/shadow/characters/wander/_cmn/wander.glb --selftest`
  — it prints the `s2-anim` line and renders one skinned frame.

The `wander`/`agro` animation pins load those glbs from `$SOTC_LIB` and require a library
rebuilt with the S2 clips (a pre-S2 library fails them). Rebuild it once with:

```bash
export SOTC_GAME_ROOT=/Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
dotnet exec out/bin/bp-convert.dll convert --root "$SOTC_GAME_ROOT" --out out/library \
  --under models/shadow/characters/wander/_cmn/wander.cmsh \
  --clips player_a_stand,player_a_walk,player_a_run --force
dotnet exec out/bin/bp-convert.dll convert --root "$SOTC_GAME_ROOT" --out out/library \
  --under models/shadow/characters/agro/_cmn/agro.cmsh \
  --clips horse_a_add_horse_kick --force
```

`--clips` bakes every named clip into every source in scope, so it is a single-model
invocation (`--under <file>`): every track must map into that model's skeleton.

## Guarantees

- **Windowed, always.** Never fullscreen; `SDL_WINDOW_RESIZABLE` from the start.
- **Cursor never captured.** `SDL_HINT_MOUSE_AUTO_CAPTURE=0` is set before
  `SDL_Init`; relative mouse mode and mouse grab are explicitly disabled (and
  never enabled anywhere). `--selftest` reports the state *queried back from SDL*,
  not flags the app assumes.
- **No Xcode Metal toolchain needed to build.** The GPU device requests MSL
  shader source (`SDL_GPU_SHADERFORMAT_MSL`), which SDL compiles at runtime:
  CMake copies `shaders/mesh.metal` next to the binary and the renderer loads it
  from `SDL_GetBasePath()`. A shader that cannot be read or compiled aborts the
  run with a non-zero exit — never a silent fallback.
- **Unskinned geometry draws in bind pose.** Every vertex is skinned through the
  same path (a primitive without JOINTS_0/WEIGHTS_0 defaults to joint 0, weight
  1), and a model without a skeleton keeps a single identity palette matrix —
  so unskinned models and sub-meshes render unchanged, never collapsed.

## Vendored dependencies

`third_party/` holds `cgltf.h`, `stb_image.h`, `stb_image_write.h`, and
`doctest.h` at pinned upstream revisions — see `third_party/VERSIONS.md`.
