# sotc-viewer

The native macOS viewer for the converted *Shadow of the Colossus* asset library:
C++20, CMake, SDL3, SDL_GPU on Metal. It consumes only converted assets
(`out/library`: `.glb`, `.png`, and the audio WAVs + manifest) — never the game tree or
original containers.

**Status (milestone S5): the sound of the forbidden land — the game's own audio decoded at
build time (the seven attributed cues plus Boss 1), a deterministic mixer, `M` to mute (user
walk pending).**
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

The encounter has its own voice. The library's converted audio slice is loaded at start
(a missing manifest degrades to silence with one warning; an encounter whose manifest lacks
a curated cue is a load error naming the event) and played through a 32-voice software mixer
into one SDL3 stream at 48 kHz: the music is non-positional, world sounds attenuate with
distance and pan by the player's relative bearing. Music starts on the first landed hit
and loops, and stops at the `R` reset (and on Valus's defeat). `M` mutes and unmutes the whole
mixer; there is no other audio UI. The curated slice is the seven attributed cues
(`player.swing`, `player.hit`, `player.hurt`, `player.death`, `valus.telegraph`,
`valus.impact`, `valus.hurt`) plus `music.boss` (`Boss 1`, 5.1 downmixed to stereo, looped).
Valus's death, the club whoosh, the player's fall, ambience, grab/climb and footsteps are
**deferred** — they are not in the cue map, so they are silent rather than placeholders.
`--s5-selftest --audio-mixdown PATH` runs the S4.2 timeline with the audio observer on and
writes the proof.

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
s5-mix frames=2139200 rms=0.0514 peak=0.9022 window_rms=0.1520
s5-status mixdown=…/out/s5/mix.wav events=18 music=1
```

The first strike (1.017 s) is the foot clang: it swings but deals no damage, so
it plays no hit and starts no music. The first landed hit starts Boss 1;
`hurt_heavy` is the same `player.hurt` clip pushed to 1.25; the music stops at
the reset (20.483 s), one step after the death, because the reset is what ends
the fight in this timeline. With audio off the timeline prints the same
`s42-*` lines byte for byte.

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
`<library>/audio/audio.manifest`; the runtime never parses `.sxd`/`.sxd2`. A cue name that is
not in the manifest is a load error naming the event, never a silent fallback.

```bash
export SOTC_GAME_ROOT=…/SOTC-extract/extracted/game
dotnet exec out/bin/bp-convert.dll audio --root "$SOTC_GAME_ROOT" --out out/library \
  --map tools/re/audio/s5_map.json --force
# -> manifest: out/library/audio/audio.manifest (8 entries)
# -> audio: 8 written, 0 reused, 8 manifest entries
```

`tools/re/audio/s5_map.json` is the slice's authority: seven attributed cues (`player.swing`,
`player.hit`, `player.hurt`, `player.death`, `valus.telegraph`, `valus.impact`,
`valus.hurt`) and `music.boss`. Only `valus.impact` is chain-verified (`NAME` → `REQD` →
`SEQD` → `PLAY_TONE` → `WAVE`); the other six are audition picks, and every 0x21 PS-ADPCM
row is a fitted-filter decode. Each row's method, identity confidence and fidelity are in
`docs/notes/s5-audio-attribution.md`; the decode itself is in
`docs/notes/s5-audio-recon.md`. **Deferred** (not in the map, therefore silent rather than
replaced): Valus's death, the club whoosh, the player's fall, ambience, grab/climb and
footsteps. The user's ear is the identity check (`docs/notes/s5-results.md`).

The mixer is a 32-voice software mix at 48 kHz float stereo into one SDL3 stream: category
gains, distance attenuation and bearing pan for world sounds, whole-track loops for music,
oldest/quietest voice stealing, a 0.85 master headroom before the clamp, and `M` to mute.
`--s5-selftest --audio-mixdown out/s5/mix.wav` proves it headlessly (no audio device); the
acceptance script (`scripts/s5-acceptance.sh`) runs it, checks the sequence / the WAV /
`ffmpeg astats`, proves the `s42-*` lines are unchanged with audio off, then runs the S4.2
acceptance unchanged.

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
