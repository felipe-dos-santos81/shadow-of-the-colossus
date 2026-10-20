#pragma once

#include <cstddef>
#include <vector>

#include "collision.h"
#include "grab.h"
#include "heightmap.h"
#include "math3d.h"
#include "player.h"
#include "scene.h"

// The player's combat state machine (S4.1 Task 3): walking, gripping a climb
// band, moving along it, holding a strike to charge, the forced fall, the catch
// window and the landing stagger.
//
//     Grounded --> Grabbing <--> Climbing --> Striking
//        ^             \            |            /
//        |              +-----------+-----------+
//        |                          v
//        +------ Staggered <----- Falling
//
// Every mode is driven by one `player_step` call per frame. Grounded runs the
// S3 controller and the S4 capsule resolver in their existing order, so the
// S3/S4 numbers cannot move. Attached modes instead recompute the attach point
// from this frame's posed skeleton and never sample the heightmap or run the
// capsule resolver. Falling integrates gravity from the release point; a grip
// break — including the one a landed strike causes — also opens the catch
// window in which a fresh grab press with stamina left re-attaches.

enum class PlayerMode { Grounded, Grabbing, Climbing, Striking, Falling, Staggered };

// The persistent combat state. The first block is the plan's contract; the
// second is the hold-to-charge bookkeeping ruling R6 needs (the plan's fixed
// swing window is superseded by "hold J, release to plunge").
struct PlayerCombat {
    PlayerMode mode = PlayerMode::Grounded;
    float modeSeconds = 0.0f;
    float stamina = 100.0f;
    float regenDelay = 0.0f;         // s left before ground regen resumes
    ClimbBandId band = ClimbBandId::Count;
    float bandT = 0.5f;              // normalized position along the band
    int weakpoint = -1;              // the attached band's weakpoint, -1 none
    bool strikeHit = false;          // this swing has emitted its one damage event
    bool grabWasDown = false;        // rising-edge memory for the grab key
    bool strikeWasDown = false;      // rising-edge memory for the strike key

    // R6/R7 strike state.
    float charge = 0.0f;            // s of J held, capped at chargeSeconds
    float catchSecondsLeft = 0.0f;  // Falling's re-grip window
    float fallVelocity = 0.0f;      // m/s, negative is down
};

// One frame's view of the world: the ground, the entity's posed skeleton (the
// source of every climb band), and the entity's collision proxy. `scene` and
// `entityRig` identify the entity for the app's weakpoint/reaction routing.
struct PlayerFrame {
    const Scene* scene = nullptr;
    const Heightmap* heightmap = nullptr;
    JointLookup jointWorld;
    std::size_t entityRig = 0;
    float cameraYawDeg = 0.0f;
    std::vector<Capsule> entityCapsules;
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
};

// What one step changed. `damageDealt` is the amount of the single damage event
// this step emitted (0 when none); the app applies it to the encounter.
struct StepEvents {
    int damageDealt = 0;
    bool gripBroke = false;
    bool landed = false;
    bool grabbed = false;
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
// most one boundary per step). A strike press enters Striking. While strike is
// held the charge grows at 1 s per second up to `chargeSeconds` and stamina
// drains at `staminaStrike`; releasing emits one damage event — `damageFull` at
// or above `fullChargeFraction` of full charge, `damageWeak` below it — and the
// hit shakes the player off immediately: `gripBroke` is reported and Falling
// begins with the catch window (ruling R7; there is no recovery mode). A grip
// break mid-charge cancels with no damage. The attach point is recomputed from
// `frame.jointWorld` every attached step and written to `position`, with
// `yawDeg` facing the surface; the heightmap and the capsule resolver are not
// touched.
//
// Falling: integrates vertical velocity at 9.8 m/s² downward, keeps XZ, and
// lands when `position.y` reaches the ground sample (the ground is only used
// when finite; a hole keeps the fall going, never a NaN). A grip break
// (stamina zero, a landed strike, or a band that can no longer be resolved)
// enters Falling with `catchSeconds` left; a fresh grab press with stamina
// left inside that window re-attaches to `nearest_climb_band(...,
// catchRange)`. A grab that never took hold is not a grip break: a failed
// initial grab stays Grounded with no event and no window. Landing enters
// Staggered, then Grounded after `staggerSeconds`.
StepEvents player_step(PlayerState& player, PlayerCombat& combat, const PlayerInput& input,
                       const PlayerFrame& frame, const CombatTuning& tuning, float dtSeconds);
