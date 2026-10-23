#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "anim.h"
#include "app.h"
#include "behaviour.h"
#include "encounter_rt.h"
#include "encounters/quadratus.h"
#include "heightmap.h"
#include "player_state.h"
#include "quadratus_motion.h"

// Quadratus's fight at runtime (S11 Task 10, design D9): the glue that plays his
// table (encounters/quadratus.h) through the generic modules — the behaviour
// machine (behaviour.h), his measured root motion and hoof rule
// (quadratus_motion.h), the surface walk and the shake-off (player_state.h). It
// owns his clock, his travel over the ground, his choices against the player,
// the hit routing through the gates and the ground stab; the app (frame_step)
// owns the clips' upload, the player and the death sequence. Pure but for the
// scene reads the caller passes in.
//
// Labels: data / measured / authored as in quadratus.h; every authored value
// below is to be tuned on the user's walk.

// Authored: he walks toward a grounded player farther than kQuadWalkStartMeters
// and stops at a walk cycle's end once within kQuadWalkStopMeters — a cycle
// carries 20 m and walk_end 6.4 m (measured, C§1), so he halts about 25-30 m
// out, at the edge of his stomp's 25 m reach (the attack row), and the player
// closes the rest.
inline constexpr float kQuadWalkStartMeters = 60.0f;
inline constexpr float kQuadWalkStopMeters = 50.0f;
// Authored: no turn for a player within this of his origin (under his belly the
// bearing is noise).
inline constexpr float kQuadTurnMinMeters = 2.0f;
// Authored: no stomp at a player nearer than this — the stomp lands 24 m ahead
// (measured, C§4); within it the player stands under his chest.
inline constexpr float kQuadAttackMinMeters = 12.0f;
// Authored: the most his ground origin rises or sinks in one frame of travel
// (heightmap_floor's step for a 14.5 m-tall body).
inline constexpr float kQuadGroundStep = 2.0f;
// The player's ground stab (the hoof rule's strike, design §11 "stab a rear
// hoof"): Wander's baked `player_a_atk1`. Authored: the stab's hit is tested on
// its first frame.
inline constexpr const char* kQuadStabClip = "player_a_atk1";

struct QuadratusFight {
    BehaviourRuntime rt{};
    // kClips rows -> the entity playback's clip index and its length.
    std::array<int, encounters::quadratus_detail::C::Count> clip{};
    std::array<float, encounters::quadratus_detail::C::Count> seconds{};
    HoofSoles soles{};
    Vec3 basePosition{};  // the manifest placement: R returns here
    float baseYawDeg = 0.0f;
    // The clip drawn last frame and its time: the root delta's origin.
    int lastClip = -1;
    float lastTime = 0.0f;
    // The attack in progress: its kAttacks row (-1 none) and its once-only impact.
    int chain = -1;
    bool impactDone = false;
    // Damage spent on each sigil, in kSigilOrder.
    std::array<float, encounters::quadratus_detail::kSigilOrder.size()> sigilSpent{};
    // The shake rule's clocks (kShakeRule): the attached cycle, the post-stab pause.
    float cycleSeconds = 0.0f;
    float stabPauseSeconds = 0.0f;
    // The ground stab: the strike key's edge memory, the stab clip's clock (< 0 none).
    bool strikeWasDown = false;
    float stabSeconds = -1.0f;
    int stabClip = -1;
    float stabDuration = 0.0f;
};

// Resolves his clips (every kClips name in `entity`), the player's stab clip
// (in `player`) and reads his soles from the glb. False, with `why`, when one
// is missing.
bool quadratus_fight_load(QuadratusFight& fight, const Scene& scene, std::size_t entityRig,
                          const Playback& entity, const Playback& player, std::string& why);

// The opening state (the stage's start and R): Stand at t 0 at the manifest
// placement (`basePosition`/`baseYawDeg`), no attack, sigils whole, clocks idle,
// the stand clip on `entity`. The Encounter's own fields are reset_encounter's.
void quadratus_fight_reset(QuadratusFight& fight, Encounter& encounter, Playback& entity);

// One choice against the player (pure). `bearingDeg`: the player's heading from
// his facing (+ = his left, the turn_l side); `walkCycleEnded`: a walk cycle
// closed this frame (he leaves the walk only there, so the pose stays whole).
enum class QuadDecision { None, TurnL, TurnR, Walk, WalkTurnL, WalkTurnR, WalkEnd, AttackL,
                          AttackR, Shake };
struct QuadSituation {
    int state = 0;
    bool walkCycleEnded = false;
    float bearingDeg = 0.0f;
    float distance = 0.0f;
    float cooldownSeconds = 0.0f;
    bool grounded = false;  // PlayerMode::Grounded exactly
    bool attached = false;  // Grabbing / Climbing / Striking
    float cycleSeconds = 0.0f;
    float stabPauseSeconds = 0.0f;
};
QuadDecision quadratus_decide(const QuadSituation& s);
const char* quad_decision_name(QuadDecision d);

// The hoof rule's one state input (carry item 2): no hoof gate in the 21
// kNoHoofGateStates (kneels, rising_return, the refuses, death) nor while the
// player is attached — the table cannot see attachment (mode 19/20 has no sole
// transition). Deviation (tagged): in the game a hit on the OTHER hoof mid-kneel
// would switch the sprain mode; here every hoof hit in a kneel is ignored.
bool quadratus_hoof_blocked(int state, bool attached);

// What one frame of his fight did (the selftest's lines read it).
struct QuadratusFrame {
    int stateBefore = 0;
    int stateAfter = 0;
    QuadDecision decision = QuadDecision::None;
    float yawAddedDeg = 0.0f;
    bool impactDue = false;  // the attack's impact window opened this frame
    int impactAttack = -1;   // its kAttacks row (0 the left stomp, 1 the right)
    bool chainEnded = false;
};

// One frame: the shake clocks, the behaviour clock (clip ends, timers, the turn
// yaw at a turn's end, the attack's cooldown at its chain's end), the root
// travel of the drawn clip (rotated by his yaw, onto the layered ground under
// his origin; a move off the map or onto no floor is refused), his choice
// against the player, and the entity playback (index and time; one-shots
// clamp, loops wrap). While defeated the death sequence owns the clip: only
// its root travel moves him (he holds its last frame: kHoldLastFrameState).
QuadratusFrame quadratus_fight_step(QuadratusFight& fight, Encounter& encounter, Playback& entity,
                                    const Heightmap& ground, const PlayerState& player,
                                    const PlayerCombat& combat, float dt);

// The shake-off this frame (kShakes: the drain inside a refuse clip's active
// window); none otherwise.
ShakeOff quadratus_shake(const QuadratusFight& fight);

// A swing released while attached (hit or whiff): the shake rule's 3 s pause.
void quadratus_fight_struck(QuadratusFight& fight);

// An attached strike's damage routed through his gates (carry item 1): a dead
// Quadratus ignores everything; Ignore rows do nothing; a hoof Transition enters
// its kneel with no damage; a sigil's Damage counts only on the open sigil of
// kSigilOrder (back, then head) and only up to its share of his health
// (authored: maxHealth split evenly, the HP formula is open — F "Open"); the
// blow that empties the last share defeats him: the death clip starts, the
// machine enters Death.
struct QuadratusHit {
    int weakpoint = -1;  // the struck weakpoint (-1: no hit this frame)
    GateKind kind = GateKind::Ignore;
    bool sealed = false;  // a Damage row on a sigil that is not open yet
    float damage = 0.0f;
    bool killed = false;
};
QuadratusHit quadratus_fight_hit(QuadratusFight& fight, Encounter& encounter, Playback& entity,
                                 const EntityClips& clips, int weakpoint, float damage);

// The open sigil's weakpoint (kSigilOrder), -1 once every share is spent.
int quadratus_open_sigil(const QuadratusFight& fight, const Encounter& encounter);

// Authored: the ground stab's sword reaches this far past the player's body
// capsule (the hoof rule's strike_reaches_shell gets the sole's shell radius plus
// it). The hoof's own collision capsule (r 2.5, the cgpr B field) holds the
// player 2.85 m off its axis, and the sole's shell radius is 2.4-2.8 m, so
// without the blade's reach no stab lands (measured in the selftest's trace:
// 3.0 m at best against 2.7 m); the recon's windows assumed it (clips.md §3).
inline constexpr float kQuadStabReachMeters = 0.6f;

// Whether a stab now at the player's feet would kneel him by kHooves[row]: the
// hoof rule (quadratus_hoof_kneels, the blocked rule above) on the toe bone posed
// from `palette`, the ground under it, the shell radius plus the stab's reach.
// `lift`, when given, receives the sole's lift (NaN unposed).
bool quadratus_hoof_window(const QuadratusFight& fight, const Encounter& encounter,
                           const Scene& scene, std::size_t entityRig,
                           const std::vector<Mat4>& palette, const Heightmap& ground,
                           const PlayerState& player, const PlayerCombat& combat,
                           const WeakpointProxyLookup& proxy, std::size_t row,
                           float* lift = nullptr);

// The ground stab (the hoof rule): a strike press while Grounded starts the stab
// clip and, on its first frame, tests every hoof with quadratus_hoof_kneels
// (its toe bone posed from `palette`, the ground under it, the player's feet,
// the blocked rule above); a hoof that kneels him enters its kneel through its
// Transition gate (no damage). `hoof` is that weakpoint, -1 when none.
struct QuadratusStab {
    bool started = false;
    int hoof = -1;
    std::array<float, 4> lift{};  // per kHooves row, NaN when unposed
};
QuadratusStab quadratus_ground_stab(QuadratusFight& fight, Encounter& encounter, const Scene& scene,
                                    std::size_t entityRig, const std::vector<Mat4>& palette,
                                    const Heightmap& ground, const PlayerState& player,
                                    const PlayerCombat& combat, const PlayerInput& input,
                                    const WeakpointProxyLookup& proxy);

// Whether the stab clip owns the player's clip this frame (Grounded, its clock
// running).
bool quadratus_stabbing(const QuadratusFight& fight, const PlayerCombat& combat);

// The state's stable name (the selftest's lines): "Stand", "DownBLStart", ...
const char* quad_state_name(int state);
