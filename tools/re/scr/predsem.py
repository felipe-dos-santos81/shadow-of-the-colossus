"""Hand-written predicate / action semantics (S12 graph recon). Keys are function addresses.
Slots of a 0x78 condition clause as the evaluator FUN_0082d9f0 passes them (binary):
  i10/i14/i18 = u32 at +0x10/+0x14/+0x18 (esi/edx/ecx), f1c/f20/f24 = float +0x1c/+0x20/+0x24 (xmm0-2),
  q28/q30 = u64 +0x28/+0x30 (r8/r9), q38 = u64 +0x38 (stack 0), h40/h50/h60 = handle {import*, packed} (stack, by address).
label: binary = read from the decompiled function; guess = from the condition labels / partial read.
app: yes = the app already has the state; new = needs new state (named); stub = constant / no-op is acceptable."""

P = {}
def p(addr, name, sem, reads, params, label, app, note=""):
    P[addr] = dict(name=name, semantic=sem, reads=reads, params=params, label=label, app=app, note=note)

# ---- the 39 the three bosses (Valus MINOTOUR_A, Quadratus MAMMOTH, Gaius KNIGHT) reach -------------------
p(0x832610, "sub", "evaluate another condition (h50) with FUN_0082d9f0; how conditions nest (AND of ORs etc.)",
  ["h50 condition"], {"h50": "scr_condition_def"}, "binary", "yes")
p(0x832bb0, "player_surface", "player's contact-surface collision attribute (FUN_00716270(player) -> u64) matches mask/value "
  "(FUN_006fb790: value==-1 -> any bit of mask; else (attr&mask)==value); with h40 the contact object must also be that gameobject. "
  "Bits 0x3f0 = boss region id (0x10 head, 0x140 L calf, 0x200 weapon...), 0x3c00000 = owner kind (0x400000 boss body)",
  ["player contact attr (u64)", "player contact object"], {"q28": "mask", "q30": "value (-1 any)", "h40": "gameobject (opt)"},
  "binary", "partial", "app has PlayerMode + ClimbBandId band/weakpoint; needs a band -> region-id (attr & 0x3f0) table per boss and an owner-kind bit")
p(0x833090, "boss_motion_is", "boss's current motion (actor+0x260) is motion h50", ["boss actor+0x260"], {"h50": "motion-def"},
  "binary", "yes", "current clip index by name")
p(0x833500, "player_control_mode_is", "player's control-mode (actor+0x9c8) is h50 (PLAYER_CONTROL_MODE: death 79, cling/edge...)",
  ["player actor+0x9c8"], {"h50": "control-mode"}, "binary", "partial", "map PLAYER_CONTROL_MODE rows -> PlayerMode")
p(0x8333e0, "player_motion_started", "player's current motion is h50 AND the player's motion switched this frame (+0x52c): "
  "player started motion X (finger whistle 604/606/608/611...)", ["player actor+0x260", "player actor+0x52c"],
  {"h50": "ph_motion_def"}, "binary", "new", "a 'whistle started this frame' event (S8 has Q whistle for Agro)")
p(0x833d50, "time_in_mode_ge", "W+0x4b50 >= f1c s: time in the current mode (zeroed only when the mode CHANGES; a self-transition keeps it)",
  ["W+0x4b50"], {"f1c": "seconds"}, "binary", "yes")
p(0x8346e0, "game_flag", "global event flag bit i10 is set (FUN_007dbba0: bit array 0x325f3b0); 0x46 boss started, 0x19e retry ...",
  ["global flag bits"], {"i10": "flag index"}, "binary", "new", "a small flag set; most uses are start-up/retry/cutscene -> constants for a fixed fight")
p(0x833200, "boss_motion_in_layers", "motion h50 is playing in any active motion layer of the boss (actor+0x470 layers, count +0x5c0)",
  ["boss motion layers"], {"h50": "motion-def"}, "binary", "partial", "app plays one clip (+ additive?) per entity")
p(0x833880, "sensed", "perception 'sensed' latch W+0x4aec (false when there is no player): seen >= 0.5 s or near >= 2 s, held "
  "for relposparam[6] s after losing (FUN_0083a6f0 / FUN_0083aae0, params from the mode's slowstatus +0x40 scr_relposparam)",
  ["W+0x4aec"], {}, "binary", "new", "perception model: near radius/height, sight dist, half-FOV, pitch window, ray, timers")
p(0x833180, "boss_motion_ended", "boss's previous motion (actor+0x268) is h50 AND the motion switched this frame (+0x52c): motion X just ended",
  ["boss actor+0x268", "boss actor+0x52c"], {"h50": "motion-def"}, "binary", "yes", "clip-end event")
p(0x833360, "player_motion_is", "player's current motion is h50 (e.g. 258 = collapsed/death, 257 stunned)",
  ["player actor+0x260"], {"h50": "ph_motion_def"}, "binary", "partial", "map the few ph_motion ids used to PlayerMode/clips")
p(0x833960, "player_dist_le", "horizontal boss->player distance W+0x4b20 <= f1c m (false without player)",
  ["W+0x4b20"], {"f1c": "metres"}, "binary", "yes")
p(0x832600, "true", "always true ('Resident;_always_true')", [], {}, "binary", "yes")
p(0x832710, "hit_region", "boss was hit this frame (hit record type 3) and (hit attr & q28) == q30 (q30 -1: any bit): "
  "the region a weapon hit landed on (0x3f0 mask; 0x140 Valus L calf, 0x380/0x3a0/0x390/0x3b0 Quadratus soles)",
  ["boss hit record (actor+0x310)"], {"q28": "mask (0x3f0)", "q30": "region (-1 any)"}, "binary", "yes", "weakpoint-hit event -> region id table")
p(0x832620, "hit", "boss was hit this frame (FUN_007d7e50: hit record type == 3) ('Resident;_I_was_attacked')",
  ["boss hit record"], {}, "binary", "yes")
p(0x832660, "hit_kind", "hit this frame with attack kind rec[0x15]: i10 0 -> kind in {0,2,4,5}; 1 -> {1,3}; else kind == i10 "
  "(kind meanings guess: sword vs arrow/stab families; Quadratus soles require NOT kind 3)",
  ["boss hit record kind"], {"i10": "kind selector"}, "binary", "partial", "app hits are sword only: kind table needed (arrow = new)")
p(0x8337e0, "arrived_goal", "path follower arrived (W+0x3d6 set and no waypoints left W+0x36c == 0)",
  ["W+0x3d6", "W+0x36c"], {}, "binary", "new", "route/patrol following")
p(0x833920, "player_dist_ge", "W+0x4b20 >= f1c m (true without player)", ["W+0x4b20"], {"f1c": "metres"}, "binary", "yes")
p(0x833a70, "player_bearing_in", "player's bearing in the boss's frame (atan2 of local x,z; deg; + = boss's ?) in [f1c, f20] (false without player)",
  ["boss rot actor+0x640", "boss/player positions"], {"f1c": "min deg", "f20": "max deg"}, "binary", "yes", "sign convention to pin")
p(0x8338b0, "not_sensed", "!W+0x4aec (true without player)", ["W+0x4aec"], {}, "binary", "new", "perception")
p(0x8350c0, "signal_arrow_present", "a kabura-ya (whistling arrow) entity exists (entity list type 7, state 0) (FUN_00841a30)",
  ["entity list"], {}, "binary", "new", "no arrows in the app -> constant false")
p(0x833d80, "time_since_transition_ge", "W+0x4b54 >= f1c s: time since the last transition (zeroed on EVERY transition incl. self)",
  ["W+0x4b54"], {"f1c": "seconds"}, "binary", "yes")
p(0x835220, "mode_just_entered", "a transition happened this frame (W+0x4b34) and the current mode is h50", ["W+0x4b34", "W+0x4b40"],
  {"h50": "scr_mode_status"}, "binary", "yes")
p(0x8332f0, "boss_control_mode_is", "boss actor control-mode (+0x9c8) is h50", ["boss actor+0x9c8"], {"h50": "control-mode"},
  "binary", "partial", "control-mode = the boss's high-level motion set; derive from current clip group")
p(0x8338e0, "perceived_now", "this frame's raw perception: seen (W+0x4af0) or near (W+0x4af1)", ["W+0x4af0", "W+0x4af1"], {},
  "binary", "new", "perception")
p(0x833680, "move_done", "locomotion job of kinds 5..8 finished (FUN_008595f0(W,5) -> W+0x4770) ('GP mode end')", ["W+0x4770"], {},
  "binary", "new", "movement-target controller")
p(0x832970, "path_to_player", "nav query from the boss toward the player (FUN_00820730/00820940 on W+0x2000); result = a WAY exists (guess)",
  ["navigation"], {}, "guess", "new", "constant true in an open arena")
p(0x833800, "lost_target", "path follower lost / cannot reach its target (FUN_0081d690 on W+0x378/+0x36c/+0x3d6)", ["path follower"], {},
  "binary", "new", "route following")
p(0x833100, "boss_motion_started", "boss's current motion is h50 AND motion switched this frame: motion X just started (issued)",
  ["boss actor+0x260", "boss actor+0x52c"], {"h50": "motion-def"}, "binary", "yes", "clip-start event")
p(0x833780, "dprelate_changed", "the route chosen by the scr_dprelate table h40 (player contact attr -> route id) differs from the "
  "current route (W+0x4738) unless the boss is already close (FUN_00859650)", ["player contact attr", "W+0x4738"],
  {"h40": "scr_dprelate"}, "binary", "new", "Quadratus hill routes")
p(0x834fb0, "demo_camera_active", "an event/demo camera is active (camera mode 9 or a demo playing)", ["camera"], {}, "binary", "stub",
  "intro camera; false once the fight runs")
p(0x832a00, "waypoint_bearing_le", "|bearing to the current path waypoint (FUN_0081d980(W,0)) in the boss's frame| <= f1c deg",
  ["path waypoint", "boss rotation"], {"f1c": "deg"}, "binary", "new", "route following")
p(0x834700, "time_since_attack_ge", "W+0x4b90 >= f1c s: time since the slowstatus attack trigger last armed (FUN_0085a960 zeroes it)",
  ["W+0x4b90"], {"f1c": "seconds"}, "binary", "yes", "a cooldown clock")
p(0x82afd0, "anger_ge", "anger W+0x4bc8 >= f1c", ["W+0x4bc8"], {"f1c": "anger"}, "binary", "new", "anger model (FUN_0082aef0)")
p(0x8327e0, "route_point_event", "the route point the follower is at has attribute (short +0x1c) == i10 (e.g. EVENT1)",
  ["route point"], {"i10": "point attribute"}, "binary", "new", "route following")
p(0x833a20, "perceived_bearing_le", "|perception bearing W+0x4af4 (rad -> deg)| <= f1c (false without player)", ["W+0x4af4"],
  {"f1c": "deg"}, "binary", "yes", "same as |player bearing| from the eye frame")
p(0x82b000, "anger_le", "anger W+0x4bc8 <= f1c", ["W+0x4bc8"], {"f1c": "anger"}, "binary", "new", "anger model")
p(0x8356b0, "swing_space_clear", "three ray tests from the boss position: forward f1c*s, forward +/- side f20*s (s = f24) all miss: "
  "room to swing (Gaius 'sword attack possible')", ["collision rays"], {"f1c": "forward m", "f20": "side m", "f24": "scale"},
  "binary", "partial", "ray against the arena heightmap/walls")
p(0x6feae0, "knight_flag0", "Gaius work bit (W'+0x4c80 & 1), set by action FUN_006fe580 ('Knight;_stop_issue')", ["W'+0x4c80"], {},
  "binary", "new", "boss-local flag word")

# ---- shared ones the three do not reach (partial reads; for later colossi) ----------------------------
p(0x833de0, "player_in_volume", "player position inside judge volume h50 (FUN_0065eba0); false while FUN_0083b0f0", ["player pos"],
  {"h50": "scr_judge_volume_def"}, "binary", "new")
p(0x833ec0, "player_in_object_volume", "player inside judge volume h50 attached to gameobject h40", ["player pos"],
  {"h40": "gameobject", "h50": "scr_judge_volume_def"}, "binary", "new")
p(0x834140, "object_motion_is", "gameobject h40's current motion is h50 (stage props: walls, towers, bridges)", ["object actor+0x260"],
  {"h40": "gameobject", "h50": "motion-def"}, "binary", "new")
p(0x8350d0, "mode_is", "the current mode (W+0x4b40) is h50", ["W+0x4b40"], {"h50": "scr_mode_status"}, "binary", "yes")
p(0x832ce0, "player_surface_b", "as player_surface (mask q28/value q30, object h40), false while FUN_0083b0f0", ["player contact attr"],
  {"q28": "mask", "q30": "value", "h40": "gameobject"}, "binary", "partial")
p(0x832f30, "boss_attr_bits", "boss actor attribute word (+0x8c0) & q28 (== q30 unless -1), only when the boss transform flag +0x140",
  ["boss actor+0x8c0"], {"q28": "mask", "q30": "value"}, "binary", "new")
p(0x8328a0, "route_point_event_b", "route point (FUN_0081d940/950) attribute == i10", ["route"], {"i10": "attr"}, "binary", "new")
p(0x832840, "next_point_event", "next waypoint's route point attribute == i10", ["route"], {"i10": "attr"}, "binary", "new")
p(0x832e50, "player_motion_switched", "the player's (or its mount's) +0x52e flag ('starting'/'splashdown' family)", ["player actor+0x52e"],
  {}, "guess", "new")
p(0x832e30, "player_riding", "player is on a horse (FUN_007d7ee0(player) != 0)", ["player mount"], {}, "binary", "yes", "S8 ride state")
p(0x835380, "player_attr2", "player attribute FUN_0070f500(player) & q28 (== q30 unless -1) (stabbed-in-region family)",
  ["player attr"], {"q28": "mask", "q30": "value"}, "binary", "new")
p(0x8347e0, "region_hp_le", "a damage region's remaining share (FUN_006cce40, region from q30) * k <= f1c", ["region hp"],
  {"f1c": "%", "q30": "region"}, "binary", "new")
p(0x834870, "region_hp_zero", "region q30 damage value == 0 (FUN_006cd010)", ["region hp"], {"q30": "region"}, "binary", "new")
p(0x835290, "cloak_equipped", "player wears the 'veritable cloak' (FUN_007dcce0(7))", ["player items"], {}, "binary", "stub")
p(0x8352f0, "dp_bits", "(q28 & W+0x4c58) == q30: the player's DP/region bits", ["W+0x4c58"], {"q28": "mask", "q30": "value"}, "binary", "new")
p(0x834500, "horse_control_mode_is", "the horse's control-mode is h50 (gallop/co-foot)", ["horse actor+0x9c8"], {"h50": "control-mode"},
  "binary", "partial")
p(0x832c50, "player_surface_range", "player attr matches any value in [q30, q38] stepping the owner field (mask q28 0x3c00000 / 0x7f000000000000)",
  ["player contact attr"], {"q28": "mask", "q30": "from", "q38": "to"}, "binary", "partial")
p(0x8339e0, "player_dist3d_le", "3-D boss->player distance W+0x4b24 <= f1c", ["W+0x4b24"], {"f1c": "metres"}, "binary", "yes")
p(0x833660, "move_done_ap", "locomotion job kinds 2..4 finished (W+0x4771) ('AP mode end')", ["W+0x4771"], {}, "binary", "new")
p(0x834770, "hp_pct_le", "boss HP% (hp +0x50 / max +0x4c * 100) <= f1c", ["boss hp"], {"f1c": "%"}, "binary", "yes")
p(0x834730, "hp_pct_ge", "boss HP% >= f1c ('not boss death' = >= 1e-5)", ["boss hp"], {"f1c": "%"}, "binary", "yes")
p(0x8347b0, "region_hp_ge", "region share * k >= f1c", ["region hp"], {"f1c": "%"}, "binary", "new")

# ---- scr_func_def actions (0x70 B, runner FUN_0082bfb0): i08/i0c = u32 (esi/edx), f10..f1c = floats (xmm0-3),
#      q20/q28 = u64 (rcx/r8), h30 (r9), h40/h50/h60 (stack) = handles by address. A list runs until fn 0. ----------
A = {}
def a(addr, name, sem, params, label, app):
    A[addr] = dict(name=name, semantic=sem, params=params, label=label, app=app)
a(0x82f5b0, "if_run", "if condition h40 -> run action list h50", {"h40": "cond", "h50": "func list"}, "binary", "yes")
a(0x82f370, "if_ctrl", "if condition h40 -> request script control h60 (set bit ctrl.bit in actor+0xbd0; the motion graph "
  "takes the edge labelled by that control from the current motion)", {"h40": "cond", "h60": "script-ctrl-def"}, "binary", "yes")
a(0x82f5e0, "run", "run action list h50 (subroutine)", {"h50": "func list"}, "binary", "yes")
a(0x831f50, "stage_unload", "unload stage/motion bundle h40 if loaded (asset streaming)", {"h40": "stage"}, "binary", "stub")
a(0x831f20, "stage_load", "load stage/motion bundle h40 if not loaded", {"h40": "stage"}, "binary", "stub")
a(0x830330, "set_flag", "set global event flag i08 (FUN_007dbb00), e.g. 0x46 boss started", {"i08": "flag"}, "binary", "yes")
a(0x8302a0, "checkpoint", "thunk FUN_007d1d70: update the restart point / warp from global table (flag 8)", {}, "binary", "stub")
a(0x8313d0, "anger_set", "anger W+0x4bc8 = f10", {"f10": "anger"}, "binary", "new")
a(0x831400, "anger_add", "anger += f10 (e.g. -50 after an angry/arm attack)", {"f10": "delta"}, "binary", "new")
a(0x82e9c0, "goal_player_ground", "path goal = player position snapped to the boss's nav ground (FUN_00823f60) + f10 up", {"f10": "dy"},
  "binary", "partial")
a(0x82e480, "goal_player", "path goal = player position + (0, f10, 0)", {"f10": "dy"}, "binary", "yes")
a(0x82e570, "goal_lastseen_y", "path goal = last-known player position (W+0x4ac0) with y = f10", {"f10": "y"}, "binary", "new")
a(0x82e500, "goal_lastseen", "path goal = last-known player position + (0, f10, 0)", {"f10": "dy"}, "binary", "new")
a(0x82e920, "route_replan", "re-plan the route toward the player (FUN_00859ad0)", {}, "binary", "new")
a(0x830360, "look_if_motion", "while the boss plays motion h40: FUN_007feb10 toward the player (look-at / aim, guess)",
  {"h40": "motion-def"}, "guess", "partial")
a(0x82e3a0, "route_by_dprelate", "pick a route id from table r9 (scr_dprelate rows: attr mask/value, object, cond) -> FUN_00859590",
  {"h30": "scr_dprelate"}, "binary", "new")
a(0x82fea0, "stage_anim", "play stage-layout animation h40 (+0x80 -> FUN_006da9f0, track i08; i0c restart)", {"h40": "stagelayout"},
  "binary", "stub")
a(0x82ef20, "ray_ctrl", "ray from a bone (h40 + offset f14/f18/f1c) +/- f10 along an axis; on hit request control h60 (Gaius)",
  {"f10": "len", "f14": "x", "f18": "y", "f1c": "z", "h40": "bone", "h60": "ctrl"}, "binary", "partial")
a(0x8325a0, "move_param", "FUN_0081cfb0(W, i08): locomotion parameter (same setter as slowstatus +0x0c; speed tier guess)",
  {"i08": "value"}, "guess", "partial")
a(0x82fae0, "turn_to_player", "if player bearing > f10 deg request 'Stop_right_turn' (ctrl 426), if < -f10 'Stop_turn_left' (427)",
  {"f10": "deg"}, "binary", "yes")
a(0x830420, "profile_by_surface", "every frame: pick the movement profile row whose (mask,value) matches the player's contact attr "
  "(after its +0x28 hold time) -> FUN_0082dc10 (gaze/move params)", {"h30": "table"}, "binary", "partial")
a(0x830400, "profile_reset", "reset that profile clock/row (W+0x4b98/0x4ba0)", {}, "binary", "partial")
a(0x701ec0, "goal_wall", "Valus: ray from the player toward the wall (f20 height) and set the path goal f10 m off the wall hit",
  {"f10": "offset", "f14": "height"}, "binary", "partial")
a(0x6fe5e0, "knight_shake", "Gaius per-frame shake-off: by the player's region (attr & 0x3f0) request Rock_knight_head/body "
  "(ctrls 123..126); bits in W'+0x4c80 time a forced stop (ctrl 127 'Knight_forced_stop') f1c/f20/f24", {"f10": "s", "f14": "s", "f18": "s"},
  "binary", "partial")
a(0x6fe5c0, "knight_set4", "Gaius: set work bit 4 (unless set)", {}, "binary", "new")
a(0x6fe5a0, "knight_set2", "Gaius: set work bit 2 (unless bit 4)", {}, "binary", "new")
a(0x6fe580, "knight_set1", "Gaius: set work bit 1", {}, "binary", "new")
