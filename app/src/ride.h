#pragma once

#include <array>
#include <string>

#include "math3d.h"

// Riding Agro (S8 Task 5, design §6 and D5, D9-D12): a pure, headless-testable
// state machine that answers "which clip does Agro play, which does the rider
// play, at what time, how fast does she go and where does she turn". `app.cpp`
// owns her position on the heightmap, the rigs, the pose blend, the camera and
// the sound; nothing here touches I/O, SDL or a clock.
//
//   OnFoot --E in reach--> Mounting --stand_ride_<s> ends--> Riding{gait}
//   Riding{Stand|Walk} --E, a walkable side--> Dismounting --getoff_fall ends--> OnFoot
//
// The clip chains (design §6; every name below is `horse_a_uma_<name>`, the
// rider plays the paired `player_a_uma_<name>` — `stand1_player` for `stand1` —
// at the same `clipTime`):
//   upshift (a counted kick):   Stand -walk_start-> Walk(walk)
//                               Walk  -run_n_start-> Trot(run_n)
//                               Trot  -run_start->   Gallop(run)
//   downshift (a tap of S):     Gallop -run_from_run_n-> Trot(run_n)
//                               Trot   -run_n_from_walk-> Walk(walk)
//                               Walk   -walk_stop->       Stand(stand1)
//   stop (S held 0.5 s):        Gallop run_stop1 -> run_stop2 -> run_stop3 -> stand1
//                               Trot   run_n_stop -> stand1;  Walk walk_stop -> stand1
//   back (S held at a standstill): back_start -> back (while held) -> back_stop -> stand1
// `gait` is the gait being entered: it changes when the start/stop/downshift clip
// begins, not when the loop is reached.
//
// The sides. `Side::Left` is the `_l` clip family and the player's x < 0 in
// Agro's frame (+Z forward, +Y up); `Side::Right` is the `_r` family and x >= 0.
// `stand_ride_l` starts at x -0.53 — on the `ABUMI_R` stirrup, which is Agro's own
// right hand — so the side follows the x sign and the clip suffix, not anatomy
// (design D11: "choose by side, not by name"). `RideContext::leftWalkable` is the
// ground on the x < 0 side, `rightWalkable` on the x >= 0 side.
//
// Blending (D7). Every clip switch sets `blendFrom` to the seconds spent in the
// blend: 0 on an input-driven switch (the new clip starts at time 0 and the
// frame's dt is not spent), the overshoot on a clip-end switch (the long frame's
// remainder carries into the next clip). It advances with `dt` and returns to -1
// once it reaches `kClipBlendSeconds`. The app snapshots the outgoing pose at the
// switch and blends it into the new clip with w = blendFrom / kClipBlendSeconds.
// A loop wrapping, or a step without a switch, never sets it.
//
// The builder's calls where the design is silent (see the task-5 report):
// - a tap of S is recognised on release (held < 0.5 s); holding past 0.5 s stops
//   instead, so a held S from a gallop stops through `run_stop1-3`, not through a
//   downshift first. A press and release inside one step is a tap.
// - S at a standstill (gait Stand on `stand1`) backs up while held; a hold that
//   stops the horse therefore rolls into backing up if it is still held at `stand1`.
//   Releasing S during `back_start` or `back` plays `back_stop` at once.
// - ruling R8: kicks and dismount presses are ignored (not counted, not queued,
//   no neigh) while a stop or downshift clip plays — `run_stop1/2/3`,
//   `run_n_stop`, `walk_stop`, `run_from_run_n`, `run_n_from_walk` — so a kick in
//   `run_stop1` cannot snap Agro from ~6.6 to ~0.1 m/s; press again once the
//   loop (or `stand1`) is reached. A kick in Back, or during the cooldown, is
//   ignored too (silently: no neigh, not counted).
// - a kick at Gallop below the refusal speed (early in `run_start`) is counted
//   (it fills the buffer) but the gait never exceeds Gallop.
// - dismount at Walk (or during `back_stop`/`walk_start`) queues: Agro stops
//   through `walk_stop` and the dismount starts on reaching `stand1`; a kick
//   cancels the queue. The side is re-chosen then; with no walkable side it is
//   dropped. When the dismount completes (OnFoot) `side` keeps the dismount's
//   side, for dismount_offset(side).
// - the dismount prefers the side mounted from, then the other walkable side.
// - Back turns at Walk's 60 °/s; the yaw rate follows `gait` (a stop clip turns
//   at Stand's 90 °/s). A positive steerX turns right: yaw decreases (yaw 0 faces
//   +Z, yaw 90 faces +X, as player.h).
// - the steer blend is the binary's 0.5 per frame of a 30 Hz game frame, made
//   dt-correct: steer += (target - steer) * (1 - 0.5^(dt / (1/30))).
// - `rider_offset` is seat-relative (see below).
// - while OnFoot the machine drives neither rig (`horseClip`/`riderClip` empty)
//   and leaves `yawDeg` alone: the app writes Agro's heading into it before the
//   mount, and reads it while she is ridden.

enum class Gait { Stand, Walk, Trot, Gallop, Back };
enum class RidePhase { OnFoot, Mounting, Riding, Dismounting };
enum class Side { Left, Right };

// Pressed = this step's press edge; reinHeld = S (or stick back) is down this
// step. steerX is the stick/keys' X in [-1, 1] (A = -1, D = +1).
struct RideInput {
    bool kickPressed = false, reinPressed = false, reinHeld = false, mountPressed = false,
         whistlePressed = false;  // the whistle is step_agro's (on foot); unused here
    float steerX = 0.0f;
};

struct RideState {
    RidePhase phase = RidePhase::OnFoot;
    Side side = Side::Left;
    Gait gait = Gait::Stand;
    std::string horseClip, riderClip;
    float clipTime = 0, blendFrom = -1;  // blendFrom >= 0 while blending
    float yawDeg = 0, steer = 0, speed = 0;
    // Ages (s) of the last three counted kicks, -1 = empty; a full buffer starts
    // the cooldown and empties. kickRefused is true on the step a kick was refused.
    std::array<float, 3> kicks{-1, -1, -1};
    float kickCooldown = 0;
    bool kickRefused = false;
    float reinSeconds = -1;       // how long S has been held, -1 = up
    bool dismountQueued = false;  // E pressed at Walk: dismount on reaching stand1
};

// The player in Agro's frame (metres; +Z her forward), and whether each side has
// walkable ground for a dismount (Left = x < 0, Right = x >= 0; see above).
struct RideContext {
    Vec3 playerInHorse{0, 0, 0};
    bool leftWalkable = true, rightWalkable = true;
};

// The binary's kick buffer and steering (horseuniqparam.sidb / FUN_006ea960),
// D11's mount reach, and the authored values (source: authored) — exported so the
// app and the tests read the same numbers.
inline constexpr float kKickWindowSeconds = 1.0f;
inline constexpr float kKickCooldownSeconds = 3.0f;
inline constexpr float kKickRefuseFraction = 0.9f;  // of the gallop speed
inline constexpr float kSteerDeadzone = 0.3f;
inline constexpr float kSteerBlend = 0.5f;
inline constexpr float kSteerFrameSeconds = 1.0f / 30.0f;  // authored: the blend's frame
inline constexpr float kReinHoldSeconds = 0.5f;            // authored
inline constexpr float kMountReach = 1.6f;                 // authored, lateral |x|
inline constexpr float kMountHalfLength = 0.6f;            // the binary's side range top, |z|
inline constexpr float kYawRateStand = 90.0f;              // authored, °/s
inline constexpr float kYawRateWalk = 60.0f;
inline constexpr float kYawRateTrot = 60.0f;
inline constexpr float kYawRateGallop = 45.0f;

// One frame. A negative or non-finite dt advances nothing. `speed` and the
// returned state's clips are consistent with ride_speed() on return.
RideState step_ride(RideState s, float dt, const RideInput& in, const RideContext& ctx);

// OnFoot, Stand, no clip, empty kick buffer — from any phase (the `R` reset).
RideState reset_ride();

// m/s along Agro's heading while Riding (0 otherwise): horse_clip_speed of the
// playing clip.
float ride_speed(const RideState& s);

// The measured root speed of an Agro clip at `t`: the loops (`stand1`, `walk`,
// `run_n`, `run`, `back`) by their mean speed, the 14 non-loop clips by the
// `root_z` segment under t * fps (clamped to the clip); unknown clips 0.
float horse_clip_speed(const std::string& clip, float t);

// Wander's root in Agro's frame over OptBone_boy (y = 0: the clip carries it).
// Mounting/Dismounting: the clip's `rider_root_xz` at clipTime, minus the seated
// root (the `getoff_l_start` frame 0 = `stand1_player`'s root, (0, -0.0769)) —
// so the offset is 0 where Riding's is, and continuous at the dismount's start and
// at the left mount's end (the right mount ends 2.9 cm off in z). The right
// dismount's `stand_getoff_fall` mirrors x (ruling R5). 0 otherwise.
Vec3 rider_offset(const RideState& s);

// Where Wander stands (Agro's frame, seat-relative as above) when a dismount on
// `side` ends: the last `stand_getoff_fall` frame, mirrored on the right.
Vec3 dismount_offset(Side side);

// The held ride keys of one frame (the app reads them from the keyboard/gamepad).
struct RideKeys {
    bool kick = false, rein = false, mount = false, whistle = false;
    float steer = 0.0f;
};

// This frame's RideInput from the held keys: a press is a key down now and up in
// `before` (last frame's keys), reinHeld/steerX are the held values. A `locked`
// frame (a door fade) presses and holds nothing, but `before` still records the
// real keys, so a key held through the lock is not a fresh press when it ends.
// The app adds its own conditions (the whistle clip gates the mount press).
RideInput ride_presses(const RideKeys& keys, RideKeys& before, bool locked);

// Ruling R11: a move the ground refused (no ground, a cliff, a wall — the app's
// heightmap_walkable) reads as a held rein while Agro is ridden in a moving gait
// (Walk/Trot/Gallop), so the stop chain runs instead of a gallop in place. It never
// backs her up: at Stand (a stop clip, stand1) or Back it adds nothing. The app
// passes last frame's refusal; `in`'s own keys pass through unchanged.
RideInput blocked_rein(RideInput in, const RideState& s, bool blocked);

// Ruling R12: the rider's seat-relative offset during the mount's 0.25 s blend,
// lerped from `onFoot` (where he stood, in the same seat-relative frame; y ignored)
// to rider_offset(s), so pressing E never snaps him onto the clip's first frame.
// rider_offset(s) everywhere else.
Vec3 mount_offset(const RideState& s, Vec3 onFoot);

// "Stand", "Walk", "Trot", "Gallop", "Back".
const char* gait_name(Gait gait);

// Agro on foot (D12, authored): the whistle within kWhistleRange makes her approach
// (trot within kApproachGallopBeyond, gallop beyond; once approaching, ruling R9's
// hysteresis: gallop above kApproachGallopAbove, trot below kApproachTrotBelow,
// else the clip is kept, so a blocked Agro at ~40 m does not flicker) until kApproachStop, then
// `neigh` -> `neigh_end` -> Idle. The app turns and moves her (horse_clip_speed of
// `clip`, the same walkability as the player) and passes `progressed` = she got
// closer last frame; kStuckWarpSeconds without progress raises `warp` for one step
// (the app places her kWarpBehind behind the player on ground). A whistle while
// she approaches or neighs, beyond the range or within the stop distance is
// ignored. The app does not step her while she is ridden.
enum class AgroPhase { Idle, Approach, Neigh };
struct AgroState {
    AgroPhase phase = AgroPhase::Idle;
    float stuckSeconds = 0;
    std::string clip;
    float clipTime = 0;
    float blendFrom = -1;  // as RideState::blendFrom
    bool warp = false;     // true on the step the stuck warp is due
};

inline constexpr float kWhistleRange = 150.0f;
inline constexpr float kApproachGallopBeyond = 40.0f;  // the whistle step's choice
inline constexpr float kApproachGallopAbove = 42.0f;   // R9
inline constexpr float kApproachTrotBelow = 38.0f;     // R9
inline constexpr float kApproachStop = 3.0f;
inline constexpr float kStuckWarpSeconds = 10.0f;  // the binary's stuck recovery
inline constexpr float kWarpBehind = 6.0f;

AgroState step_agro(AgroState a, float dt, bool whistled, float distanceToPlayer, bool progressed);
