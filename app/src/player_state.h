#pragma once

#include <cstddef>
#include <functional>
#include <span>
#include <vector>

#include "camera.h"
#include "collision.h"
#include "grab.h"
#include "heightmap.h"
#include "math3d.h"
#include "player.h"
#include "scene.h"

// The player's combat state machine (S4.1 Task 3): walking, gripping a climb
// band, moving along it, holding a strike to charge, the forced fall, the catch
// window and the landing stagger. S4.2 adds the damage reactions and death.
//
//     Grounded --> Grabbing <--> Climbing --> Striking
//        ^             \            |            /
//        |              +-----------+-----------+
//        |                          v
//        +------ Staggered <----- Falling
//
//     any ground mode --damage--> Hurt --> Grounded
//     any ground mode --lethal--> Dead (terminal until the R soft reset)
//     Falling --lethal--> pendingDeath, resolved Dead or Staggered on landing
//
// Every mode is driven by one `player_step` call per frame. Grounded runs the
// S3 controller and the S4 capsule resolver in their existing order, so the
// S3/S4 numbers cannot move. Attached modes instead recompute the attach point
// from this frame's posed skeleton and never sample the heightmap or run the
// capsule resolver. Falling integrates gravity from the release point; a grip
// break — including the one a landed strike causes — also opens the catch
// window in which a fresh grab press with stamina left re-attaches.

enum class PlayerMode {
    Grounded,
    Grabbing,
    Climbing,
    Striking,
    Falling,
    Staggered,
    Hurt,
    Dead
};

// The mode's stable lowercase status token (`grounded`, `grabbing`, `climbing`,
// `striking`, `falling`, `staggered`, `hurt`, `dead`) for the s41-status line.
// Never null.
const char* player_mode_name(PlayerMode mode);

// The persistent combat state. The first block is the plan's contract; the
// second is the hold-to-charge bookkeeping ruling R6 needs (the plan's fixed
// swing window is superseded by "hold J, release to plunge"); the last is the
// S4.2 health/death state.
struct PlayerCombat {
    PlayerMode mode = PlayerMode::Grounded;
    float modeSeconds = 0.0f;
    float stamina = 100.0f;
    float regenDelay = 0.0f;         // s left before ground regen resumes
    ClimbBandId band = ClimbBandId::None;
    float bandT = 0.5f;              // normalized position along the band
    int weakpoint = -1;              // the attached band's weakpoint, -1 none
    bool strikeHit = false;          // this swing has emitted its one damage event
    bool grabWasDown = false;        // rising-edge memory for the grab key
    bool strikeWasDown = false;      // rising-edge memory for the strike key

    // R6/R7 strike state.
    float charge = 0.0f;            // s of J held, capped at chargeSeconds
    float catchSecondsLeft = 0.0f;  // Falling's re-grip window
    float fallVelocity = 0.0f;      // m/s, negative is down

    // S4.2 health and damage state.
    float health = 100.0f;
    float invulnSeconds = 0.0f;  // i-frame window left; > 0 ignores damage
    bool heavyHit = false;       // this Hurt came from a heavy attack
    bool pendingDeath = false;   // lethal damage taken while Falling
};


// One frame's view of the world: the ground, the entity's posed skeleton (the
// source of every climb band), and the entity's collision proxy. `scene` and
// `entityRig` identify the entity for the app's weakpoint/reaction routing.
//
// `weakpointProxy` resolves a weakpoint id to its posed shell sphere (centre and
// radius) on this frame's skeleton. It is empty when the app has no proxy; a
// strike then deals no damage. The app binds it to posed_weakpoint_proxy.
using WeakpointProxyLookup = std::function<bool(int weakpoint, Vec3& center, float& radius)>;

// S11 shake-off (design D9): what the entity does this frame against an attached
// climber (Quadratus's refuse_* clips; the strength is the encounter's data).
// Both default to nothing, so a frame without a shake steps exactly as before.
// While attached (Grabbing/Climbing/Striking, grab held), `staminaDrain` (/s,
// ignored unless finite and positive) adds to the mode's own rate — reaching
// zero is the usual grip break — and `forceRelease` throws the climber off at
// once: a grip break with the catch window, before any climb or strike this
// step, so a strike released on that step deals nothing. On the ground or in
// the air it does nothing.
struct ShakeOff {
    float staminaDrain = 0.0f;
    bool forceRelease = false;
};

struct PlayerFrame {
    const Scene* scene = nullptr;
    const Heightmap* heightmap = nullptr;
    JointLookup jointWorld;
    WeakpointProxyLookup weakpointProxy;
    std::size_t entityRig = 0;
    float cameraYawDeg = 0.0f;
    std::vector<Capsule> entityCapsules;
    ShakeOff shake;
    // The encounter's climb bands (S11 Task 7): grab, climb and catch walk this
    // table; the default is Valus's.
    std::span<const ClimbBand> bands = encounters::kValus.bands;
};

// The pinned S4.1 constants. Ruling R6 replaces the plan's `strikeSeconds` /
// `strikeHitFrom` / `strikeHitTo` with the charge and its full-charge
// threshold; ruling R7 removes the post-release recovery entirely.
struct CombatTuning {
    float grabRange = 0.9f;      // m, surface distance for a fresh grab
    float climbSpeed = 1.2f;     // m/s along the band
    float staminaHang = 2.5f;    // /s while Grabbing
    float staminaClimb = 4.0f;   // /s while Climbing
    float staminaStrike = 8.0f;  // /s while charging a strike
    float staminaRegen = 25.0f;  // /s on the ground
    float staminaRegenDelay = 1.0f;  // s on the ground before regen starts
    float chargeSeconds = 0.9f;      // full charge time
    float fullChargeFraction = 0.75f;  // release at or above this is a plunge
    float catchSeconds = 0.6f;         // re-grip window after a grip break
    float catchRange = 2.5f;           // m, surface distance for the catch
    float staggerSeconds = 1.2f;       // landing stagger
    float damageWeak = 20.0f;
    float damageFull = 34.0f;

    // S4.2 player health (design §7.5).
    float maxHealth = 100.0f;
    float invulnSeconds = 1.0f;      // i-frame window after a hit
    // Valus's attack damages (his EncounterDesc's attack rows, S11).
    float stompDamage = encounters::kValus.attacks[0].damage;       // direct stomp
    float stompWaveDamage = encounters::kValus.attacks[0].waveDamage;  // stomp shockwave
    float clubDamage = encounters::kValus.attacks[2].damage;        // club swing
    float mediumHurtSeconds = 0.40f;  // m_fly2, one clip
    float heavyHurtSeconds = 1.23f;   // h_fly1 0.60 s + h_fly_end 0.63 s
};

// What one step changed. `damageDealt` is the amount of the single damage event
// this step emitted (0 when none); the app applies it to the encounter.
//
// The S5 seam (design §7.4) adds report-only flags: the state machine never
// plays a sound or touches a device, it says what happened and the app's
// translation layer decides. `struck` is the release of a swing (a hit or a
// whiff); `hurtLight`/`hurtHeavy` are damage taken (`heavy` selects the long
// reaction); `died` is the death mode being entered.
struct StepEvents {
    int damageDealt = 0;
    bool gripBroke = false;
    bool landed = false;
    bool grabbed = false;
    bool struck = false;
    bool hurtLight = false;
    bool hurtHeavy = false;
    bool died = false;
};

// Advances the state machine one frame and returns its events.
//
// `dtSeconds` is clamped into [0, 0.05] exactly like `player_update`, so a
// frame-time spike cannot skip a band or overcharge a strike.
//
// Grounded: `player_update` then `resolve_player_collision(player,
// frame.entityCapsules, *frame.heightmap)` — the S3 order — then stamina regen
// (25/s after a 1 s delay) and a grab on a rising edge when a band surface is
// within `grabRange` and stamina is above zero.
//
// Attached (Grabbing/Climbing/Striking): releasing the grab key drops into
// Falling with no damage and no catch window. A forward/back axis moves `bandT`
// by `climbSpeed * dt / segmentLength`, entering `up`/`down` at a band end (at
// most one boundary per step). A lateral axis (`right - left`) also counts as
// climbing and orbits the attach point around the band's own axis: the axis is
// oriented upward and the angle is
// `+lateral * climbSpeed * dt / (radius + kPlayerCapsuleRadius)` (a Rodrigues
// rotation through the band segment), so D moves along the player's right
// (forward = -normal, right = forward x +Y); `bandT` is unchanged. At the top
// of a band, forward plus any lateral input follows `branch`
// when the band has one, otherwise `up`; forward alone always follows `up`.
// A strike press enters Striking. While strike is
// held the charge grows at 1 s per second up to `chargeSeconds` and stamina
// drains at `staminaStrike`; releasing emits damage — `damageFull` at or above
// `fullChargeFraction` of full charge, `damageWeak` below it — only when the
// band's weakpoint resolves through `frame.weakpointProxy` to a shell sphere
// and the closest point on the player's capsule segment (feet to feet +
// `kPlayerCapsuleHeight`) is within its radius. A whiff deals 0 and sets no
// `strikeHit`. Either way the swing shakes the player off immediately:
// `gripBroke` is reported and Falling begins with the catch window (ruling R7;
// there is no recovery mode). A grip break mid-charge cancels with no damage.
// The attach point is recomputed from `frame.jointWorld` every attached step
// and written to `position`, with `yawDeg` facing the surface; the heightmap
// and the capsule resolver are not touched.
//
// Falling: integrates vertical velocity at 9.8 m/s² downward, keeps XZ, and
// lands when `position.y` reaches the ground sample (the ground is only used
// when finite; a hole keeps the fall going, never a NaN). A grip break
// (stamina zero, a landed strike, or a band that can no longer be resolved)
// enters Falling with `catchSeconds` left; a fresh grab press with stamina
// left inside that window re-attaches to `nearest_climb_band(...,
// catchRange)`. A grab that never took hold is not a grip break: a failed
// initial grab stays Grounded with no event and no window. Landing enters
// Staggered, then Grounded after `staggerSeconds`; a landing with
// `pendingDeath` set enters Dead instead.
//
// Hurt: the reaction clock runs until `mediumHurtSeconds` or
// `heavyHurtSeconds`, then the player returns to Grounded. Dead is terminal
// until `reset_player_combat`: it ignores input and never leaves the mode. The
// i-frame counter ticks down in every mode. The S5 report flags are set on the
// release (`struck`, hit or whiff) and on a landing that resolves
// `pendingDeath` (`died`); a step never plays a sound or touches a device.
StepEvents player_step(PlayerState& player, PlayerCombat& combat, const PlayerInput& input,
                       const PlayerFrame& frame, const CombatTuning& tuning, float dtSeconds);

// The one damage entry point (design §7.5). Returns true when the hit landed,
// false when it was ignored (Dead, an active i-frame window, or a non-positive
// or non-finite amount).
//
// A landed hit subtracts HP (clamped to [0, maxHealth]) and starts the i-frame
// window. On the ground — Grounded, Grabbing, Climbing, Striking or Staggered —
// it enters Hurt; Staggered therefore cancels into Hurt. A lethal hit on the
// ground enters Dead at once. Damage taken while Falling never cancels the
// fall: it keeps health/state changes, and a lethal hit only sets
// `pendingDeath`, which `player_step` resolves on landing (Dead instead of
// Staggered). Hurt lasts `mediumHurtSeconds` or `heavyHurtSeconds`; `heavyHit`
// records which and selects the reaction clip.
//
// `events`, when given, receives the S5 report flags for the same hit: a
// non-lethal hit sets `hurtLight`/`hurtHeavy` (also in the air — the hit is
// heard), a ground-lethal hit sets `died` only. The call never plays a sound.
//
// The function does not move the player. Attacks cannot target an attached
// player (design §7.4), so the Hurt entry from an attached mode is a defensive
// path rather than a reachable fight state.
bool player_take_damage(PlayerCombat& combat, float damage, bool heavy,
                        const CombatTuning& tuning, StepEvents* events = nullptr);

// The R soft reset (design §7.5): the player goes back to the manifest spawn
// with full HP and stamina, no i-frames, no pending reactions and no stale
// climb/strike state. The caller resets the encounter (reset_encounter in
// app.h), the clip playbacks and the camera; this helper owns only the
// player's own state.
void reset_player_combat(PlayerState& player, PlayerCombat& combat, Vec3 spawnPosition,
                         float spawnYawDeg, const CombatTuning& tuning);

// S11 Task 7 fix: moved here from encounter_rt so a unit test can drive it.
// Enters the climb orbit on the first attached frame (or on F, `recenter`),
// seeded outward along the band's surface normal; every later attached frame
// just keeps the orbit on the attach point. The player position *is* the
// attach point (the state machine wrote the band surface into it). A band that
// cannot be resolved enters with a zero normal, which keeps the current yaw.
void update_climb_camera(Camera& camera, CameraClimb& climb, const PlayerState& player,
                         const PlayerCombat& combat, const PlayerFrame& frame, float dragDx,
                         float dragDy, float wheelDelta, bool recenter, float dtSeconds);
