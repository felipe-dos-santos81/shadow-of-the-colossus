#include "player_state.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kMaxStepSeconds = 0.05f;  // a frame spike cannot skip a band
constexpr float kGravity = 9.8f;          // m/s^2, falling
constexpr float kMaxStamina = 100.0f;     // the pinned pool
constexpr float kInvulnEpsilon = 1e-4f;   // float drift at the end of i-frames

// A frame with no heightmap still has defined ground behaviour: the empty grid
// has no nodes, so player_update clamps nothing and keeps y.
const Heightmap& ground_of(const PlayerFrame& frame) {
    static const Heightmap kNoGround{};
    return frame.heightmap != nullptr ? *frame.heightmap : kNoGround;
}

// The band parameter nearest `point` along its segment, clamped to [0, 1]. The
// fallback is the plan's 0.5 when the segment cannot be formed.
float nearest_t(const ClimbBand& band, const JointLookup& lookup, Vec3 point, float fallback) {
    Vec3 a{};
    Vec3 b{};
    if (!climb_band_segment(band, lookup, a, b)) {
        return fallback;
    }
    const Vec3 ab{b.x - a.x, b.y - a.y, b.z - a.z};
    const float lengthSquared = vec3_dot(ab, ab);
    if (!(lengthSquared > 0.0f)) {
        return fallback;
    }
    const Vec3 ap{point.x - a.x, point.y - a.y, point.z - a.z};
    const float t = vec3_dot(ap, ab) / lengthSquared;
    if (!std::isfinite(t)) {
        return fallback;
    }
    return std::clamp(t, 0.0f, 1.0f);
}

// Rodrigues rotation of `point` about the line through `origin` with unit
// `axis`, by `angle` radians (right-handed):
// v' = v cos a + (axis x v) sin a + axis (axis . v)(1 - cos a).
Vec3 rotate_about_axis(Vec3 point, Vec3 origin, Vec3 axis, float angle) {
    const Vec3 v{point.x - origin.x, point.y - origin.y, point.z - origin.z};
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    const float one_minus_c = 1.0f - c;
    const Vec3 cross = vec3_cross(axis, v);
    const float along = vec3_dot(axis, v);
    return Vec3{origin.x + v.x * c + cross.x * s + axis.x * along * one_minus_c,
                origin.y + v.y * c + cross.y * s + axis.y * along * one_minus_c,
                origin.z + v.z * c + cross.z * s + axis.z * along * one_minus_c};
}

// Orbits the attach point around the band segment `a -> b` (S4.2 design §7.3,
// ruling R6): a surface step of climbSpeed * dt at the capsule's radius. The
// axis is oriented upward first, so a positive lateral (D) is a positive
// rotation about it: the player faces the band (forward = -normal) and reads
// right as forward x +Y, so D moves along the player's right. The current
// position is last frame's surface point, so rotating it moves the radial hint
// that write_surface projects back onto the band; the attach height and bandT
// are untouched.
void orbit_about_segment(PlayerState& player, Vec3 a, Vec3 b, float bandRadius,
                         float lateral, float speed, float dt) {
    Vec3 axis = vec3_normalize(Vec3{b.x - a.x, b.y - a.y, b.z - a.z});
    if (axis.y < 0.0f) {
        axis = Vec3{-axis.x, -axis.y, -axis.z};
    }
    const float surface_radius = bandRadius + kPlayerCapsuleRadius;
    if (!std::isfinite(surface_radius) || !(surface_radius > 0.0f)) {
        return;
    }
    const float angle = lateral * speed * dt / surface_radius;
    if (!std::isfinite(angle)) {
        return;
    }
    player.position = rotate_about_axis(player.position, a, axis, angle);
}

void select_band(PlayerCombat& combat, const PlayerFrame& frame, ClimbBandId band, float t) {
    combat.band = band;
    combat.bandT = std::clamp(std::isfinite(t) ? t : 0.5f, 0.0f, 1.0f);
    combat.weakpoint = climb_band(frame.bands, band).weakpoint;
}

// The band reachable from `band` in the chain direction whose segment this
// frame can form. A rig may collapse a chain segment to a point — Valus's
// `Bone_KOSI` and `Bone_HARA` share one position, so the Waist band between
// them is zero-length — and a zero-length band has no surface to hold. Skipping
// it keeps the foot -> head route continuous instead of detaching the climber
// on a segment the table happens to contain; a band with two distinct posed
// joints is returned unchanged. None terminates a chain with no such band.
ClimbBandId next_climbable_band(const PlayerFrame& frame, ClimbBandId band, bool ascending) {
    int guard = 0;
    while (band != ClimbBandId::None && guard < static_cast<int>(frame.bands.size())) {
        Vec3 a{};
        Vec3 b{};
        const ClimbBand& record = climb_band(frame.bands, band);
        if (climb_band_segment(record, frame.jointWorld, a, b)) {
            return band;
        }
        band = ascending ? record.up : record.down;
        ++guard;
    }
    return ClimbBandId::None;
}

void attach_band(PlayerCombat& combat, const PlayerFrame& frame, ClimbBandId band, float t) {
    combat.mode = PlayerMode::Grabbing;
    combat.modeSeconds = 0.0f;
    combat.strikeHit = false;
    combat.charge = 0.0f;
    combat.catchSecondsLeft = 0.0f;
    select_band(combat, frame, band, t);
}

// Leaves the entity. `broke_grip` distinguishes a forced loss (reported and
// given a catch window) from a deliberate E release (no window).
void detach(PlayerCombat& combat, StepEvents& events, bool brokeGrip, float catchSeconds) {
    combat.mode = PlayerMode::Falling;
    combat.modeSeconds = 0.0f;
    combat.fallVelocity = 0.0f;
    combat.catchSecondsLeft = std::max(0.0f, std::isfinite(catchSeconds) ? catchSeconds : 0.0f);
    combat.charge = 0.0f;
    // `strikeHit` is deliberately left as it was: a landed hit stays true for
    // the fall, and a mid-charge break was never a hit. attach_band clears it.
    combat.band = ClimbBandId::None;
    combat.bandT = 0.5f;
    combat.weakpoint = -1;
    if (brokeGrip) {
        events.gripBroke = true;
    }
}

// Recomputes the attach point from this frame's skeleton and writes it to the
// player. The previous position is the radial hint, so the climber stays on the
// side of the body he grabbed. Returns false when the band cannot be resolved
// (missing joints, a degenerate segment, non-finite output).
bool write_surface(PlayerState& player, PlayerCombat& combat, const PlayerFrame& frame) {
    Vec3 point{};
    Vec3 normal{};
    if (!climb_surface_point(climb_band(frame.bands, combat.band), frame.jointWorld, combat.bandT,
                             player.position, point, normal)) {
        return false;
    }
    player.position = point;
    player.yawDeg = std::atan2(-normal.x, -normal.z) / kDegToRad;
    return true;
}

// Attaches at the band's nearest parameter and puts the player on the surface.
// A band the solver found but the surface writer rejects leaves the player in
// `fallback` with no band: a grab that never took hold is not a grip break, so
// it reports no event and opens no catch window.
bool try_attach(PlayerState& player, PlayerCombat& combat, const PlayerFrame& frame,
                ClimbBandId band, PlayerMode fallback) {
    attach_band(combat, frame, band,
                nearest_t(climb_band(frame.bands, band), frame.jointWorld, player.position,
                          0.5f));
    if (!write_surface(player, combat, frame)) {
        combat.mode = fallback;
        combat.modeSeconds = 0.0f;
        combat.charge = 0.0f;
        combat.strikeHit = false;
        combat.catchSecondsLeft = 0.0f;
        combat.band = ClimbBandId::None;
        combat.bandT = 0.5f;
        combat.weakpoint = -1;
        return false;
    }
    return true;
}

void drain_stamina(PlayerCombat& combat, float rate, float dt) {
    const float stamina = std::isfinite(combat.stamina) ? combat.stamina : 0.0f;
    combat.stamina = std::clamp(stamina - rate * dt, 0.0f, kMaxStamina);
}

// Whether a release at `feet` reaches the shell of `weakpoint` (design §7.2).
// The hit point is the closest point on the player's capsule *segment* — feet
// to feet + kPlayerCapsuleHeight — to the shell centre; the hit lands when that
// distance is within the shell radius. The segment, not the feet: the S4.1 head
// strike's capsule top sits ~1.27 m from the shell centre while the feet sit
// ~2.14 m away. player_capsule_distance is shared with Valus's attack gate, so
// the two reach tests cannot measure different shapes. A band with no
// weakpoint, an empty or failing lookup, or a non-finite shell answers false.
bool strike_reaches_shell(Vec3 feet, int weakpoint, const WeakpointProxyLookup& lookup) {
    if (weakpoint < 0 || !lookup) {
        return false;
    }
    Vec3 center{};
    float radius = 0.0f;
    if (!lookup(weakpoint, center, radius)) {
        return false;
    }
    if (!std::isfinite(feet.x) || !std::isfinite(feet.y) || !std::isfinite(feet.z) ||
        !std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z) ||
        !std::isfinite(radius) || radius < 0.0f) {
        return false;
    }
    return player_capsule_distance(feet, center) <= radius;
}

// Removes every field that only an attachment, charge or fall owns. Health,
// mode and modeSeconds stay the caller's business, so Hurt, Dead and the R
// reset share one sweep instead of three drifting copies.
void clear_fight_attachment(PlayerCombat& combat) {
    combat.band = ClimbBandId::None;
    combat.bandT = 0.5f;
    combat.weakpoint = -1;
    combat.charge = 0.0f;
    combat.strikeHit = false;
    combat.catchSecondsLeft = 0.0f;
    combat.fallVelocity = 0.0f;
}

// Leaves every fight-bearing attachment/rumble field behind and enters the
// ground damage reaction. `heavy` selects the long h_fly1 -> h_fly_end pair.
void enter_hurt(PlayerCombat& combat, bool heavy) {
    combat.mode = PlayerMode::Hurt;
    combat.modeSeconds = 0.0f;
    combat.heavyHit = heavy;
    combat.pendingDeath = false;
    clear_fight_attachment(combat);
}

// The terminal ground state: the app plays the death clip once and pins it.
// A dead player has no attachment, charge or pending fall left.
void enter_dead(PlayerCombat& combat) {
    combat.mode = PlayerMode::Dead;
    combat.modeSeconds = 0.0f;
    combat.heavyHit = false;
    combat.pendingDeath = false;
    clear_fight_attachment(combat);
}

}  // namespace

const char* player_mode_name(PlayerMode mode) {
    switch (mode) {
        case PlayerMode::Grounded:
            return "grounded";
        case PlayerMode::Grabbing:
            return "grabbing";
        case PlayerMode::Climbing:
            return "climbing";
        case PlayerMode::Striking:
            return "striking";
        case PlayerMode::Falling:
            return "falling";
        case PlayerMode::Staggered:
            return "staggered";
        case PlayerMode::Hurt:
            return "hurt";
        case PlayerMode::Dead:
            return "dead";
    }
    return "unknown";
}

StepEvents player_step(PlayerState& player, PlayerCombat& combat, const PlayerInput& input,
                       const PlayerFrame& frame, const CombatTuning& tuning, float dtSeconds) {
    StepEvents events;
    const float dt =
        std::clamp(std::isfinite(dtSeconds) ? dtSeconds : 0.0f, 0.0f, kMaxStepSeconds);
    combat.modeSeconds += dt;
    const float invuln =
        (std::isfinite(combat.invulnSeconds) ? combat.invulnSeconds : 0.0f) - dt;
    combat.invulnSeconds = invuln > kInvulnEpsilon ? invuln : 0.0f;

    const bool grabPressed = input.grab && !combat.grabWasDown;
    const bool strikePressed = input.strike && !combat.strikeWasDown;

    switch (combat.mode) {
        case PlayerMode::Grounded: {
            // The S3 order, unchanged: controller first, then entity capsules
            // (which reapply the ground sample at the resolved XZ).
            const Heightmap& ground = ground_of(frame);
            player_update(player, input, dt, ground, frame.cameraYawDeg);
            resolve_player_collision(player, frame.entityCapsules, ground);

            if (combat.regenDelay > 0.0f) {
                combat.regenDelay = std::max(0.0f, combat.regenDelay - dt);
            } else {
                const float stamina = std::isfinite(combat.stamina) ? combat.stamina : 0.0f;
                combat.stamina = std::min(kMaxStamina, stamina + tuning.staminaRegen * dt);
            }

            if (grabPressed && combat.stamina > 0.0f) {
                const ClimbBandId band =
                    nearest_climb_band(frame.bands, frame.jointWorld, player.position,
                                       tuning.grabRange);
                if (band != ClimbBandId::None) {
                    if (try_attach(player, combat, frame, band, PlayerMode::Grounded)) {
                        events.grabbed = true;
                    }
                }
            }
            break;
        }

        case PlayerMode::Grabbing:
        case PlayerMode::Climbing:
        case PlayerMode::Striking: {
            // E is held, not toggled: letting go is an immediate deliberate
            // release, not a grip break.
            if (!input.grab) {
                detach(combat, events, /*brokeGrip=*/false, 0.0f);
                break;
            }
            if (frame.shake.forceRelease) {
                detach(combat, events, /*brokeGrip=*/true, tuning.catchSeconds);
                break;
            }

            if (strikePressed && (combat.mode == PlayerMode::Grabbing ||
                                  combat.mode == PlayerMode::Climbing)) {
                combat.mode = PlayerMode::Striking;
                combat.modeSeconds = 0.0f;
                combat.charge = 0.0f;
                combat.strikeHit = false;
            }

            if (combat.mode == PlayerMode::Grabbing || combat.mode == PlayerMode::Climbing) {
                const ClimbBand& band = climb_band(frame.bands, combat.band);
                const float axis =
                    (input.forward ? 1.0f : 0.0f) - (input.back ? 1.0f : 0.0f);
                const float lateral =
                    (input.right ? 1.0f : 0.0f) - (input.left ? 1.0f : 0.0f);
                const bool moving = axis != 0.0f || lateral != 0.0f;
                // Lateral input is climbing too: it orbits around the band.
                const PlayerMode wanted = moving ? PlayerMode::Climbing : PlayerMode::Grabbing;
                if (wanted != combat.mode) {
                    combat.mode = wanted;
                    combat.modeSeconds = 0.0f;
                }
                // One segment resolution serves both the orbit and the climb; an
                // idle hang leaves write_surface to resolve the band as before.
                Vec3 a{};
                Vec3 b{};
                const bool resolved =
                    moving && climb_band_segment(band, frame.jointWorld, a, b);
                if (lateral != 0.0f && resolved) {
                    orbit_about_segment(player, a, b, band.radius, lateral,
                                        tuning.climbSpeed, dt);
                }
                if (axis != 0.0f && resolved) {
                    const Vec3 ab{b.x - a.x, b.y - a.y, b.z - a.z};
                    const float length = std::sqrt(vec3_dot(ab, ab));
                    if (length > 0.0f) {
                        const float t =
                            combat.bandT + axis * tuning.climbSpeed * dt / length;
                        if (t >= 1.0f) {
                            // W + lateral takes the branch when the band has
                            // one; W alone always follows the chain up.
                            ClimbBandId next = band.up;
                            if (lateral != 0.0f && band.branch != ClimbBandId::None) {
                                next = band.branch;
                            }
                            next = next_climbable_band(frame, next, /*ascending=*/true);
                            if (next != ClimbBandId::None) {
                                select_band(combat, frame, next, 0.0f);
                            } else {
                                combat.bandT = 1.0f;
                            }
                        } else if (t <= 0.0f) {
                            const ClimbBandId down = next_climbable_band(
                                frame, band.down, /*ascending=*/false);
                            if (down != ClimbBandId::None) {
                                select_band(combat, frame, down, 1.0f);
                            } else {
                                combat.bandT = 0.0f;
                            }
                        } else {
                            combat.bandT = t;
                        }
                    }
                }
            } else {  // Striking: hold to charge, release to plunge once.
                if (input.strike) {
                    combat.charge = std::min(combat.charge + dt, tuning.chargeSeconds);
                } else {
                    // S5: the release is the swing itself, hit or whiff.
                    events.struck = true;
                    const float threshold =
                        tuning.fullChargeFraction * tuning.chargeSeconds;
                    const float damage =
                        combat.charge >= threshold ? tuning.damageFull : tuning.damageWeak;
                    // §7.2: only a shell hit deals damage; a whiff is a clang.
                    // `strikeHit` stays false so the encounter plays no reaction.
                    if (strike_reaches_shell(player.position, combat.weakpoint,
                                             frame.weakpointProxy)) {
                        events.damageDealt = static_cast<int>(std::lround(damage));
                        combat.strikeHit = true;
                    }
                    // R7: the swing always shakes the climber off at once (the
                    // grip break is unconditional, so a whiff keeps the rhythm);
                    // the catch window opens with it.
                    detach(combat, events, /*brokeGrip=*/true, tuning.catchSeconds);
                    break;
                }
            }

            // Ride this frame's pose. Missing joints break the grip.
            if (!write_surface(player, combat, frame)) {
                detach(combat, events, /*brokeGrip=*/true, tuning.catchSeconds);
                break;
            }

            float rate = 0.0f;
            if (combat.mode == PlayerMode::Grabbing) {
                rate = tuning.staminaHang;
            } else if (combat.mode == PlayerMode::Climbing) {
                rate = tuning.staminaClimb;
            } else if (combat.mode == PlayerMode::Striking && !combat.strikeHit &&
                       input.strike) {
                rate = tuning.staminaStrike;
            }
            if (std::isfinite(frame.shake.staminaDrain) && frame.shake.staminaDrain > 0.0f) {
                rate += frame.shake.staminaDrain;
            }
            if (rate > 0.0f) {
                drain_stamina(combat, rate, dt);
                if (combat.stamina <= 0.0f) {
                    combat.stamina = 0.0f;
                    detach(combat, events, /*brokeGrip=*/true, tuning.catchSeconds);
                }
            }
            break;
        }

        case PlayerMode::Falling: {
            if (!std::isfinite(player.position.y)) {
                player.position.y = 0.0f;  // never fall on a NaN position
                combat.fallVelocity = 0.0f;
            }

            // A catch still needs stamina: with an empty pool the press is
            // ignored and the existing window simply runs out.
            if (grabPressed && combat.catchSecondsLeft > 0.0f && combat.stamina > 0.0f) {
                const ClimbBandId band =
                    nearest_climb_band(frame.bands, frame.jointWorld, player.position,
                                       tuning.catchRange);
                if (band != ClimbBandId::None) {
                    if (try_attach(player, combat, frame, band, PlayerMode::Falling)) {
                        events.grabbed = true;
                    }
                } else {
                    combat.catchSecondsLeft = 0.0f;  // the press was the catch attempt
                }
            }
            if (combat.mode != PlayerMode::Falling) {
                break;  // caught: no gravity this step
            }

            combat.fallVelocity -= kGravity * dt;
            player.position.y += combat.fallVelocity * dt;

            if (frame.heightmap != nullptr) {
                const float ground = heightmap_floor(*frame.heightmap, player.position, kFloorStep);
                if (std::isfinite(ground) && player.position.y <= ground) {
                    player.position.y = ground;
                    combat.fallVelocity = 0.0f;
                    combat.catchSecondsLeft = 0.0f;
                    events.landed = true;
                    if (combat.pendingDeath) {
                        // The lethal hit taken in the air resolves here: the
                        // death clip replaces the landing stagger.
                        enter_dead(combat);
                        events.died = true;
                    } else {
                        combat.mode = PlayerMode::Staggered;
                        combat.modeSeconds = 0.0f;
                    }
                }
            }
            if (combat.mode == PlayerMode::Falling) {
                combat.catchSecondsLeft = std::max(0.0f, combat.catchSecondsLeft - dt);
            }
            break;
        }

        case PlayerMode::Staggered: {
            if (combat.modeSeconds >= tuning.staggerSeconds) {
                combat.mode = PlayerMode::Grounded;
                combat.modeSeconds = 0.0f;
                combat.regenDelay = tuning.staminaRegenDelay;
            }
            break;
        }

        case PlayerMode::Hurt: {
            const float duration =
                combat.heavyHit ? tuning.heavyHurtSeconds : tuning.mediumHurtSeconds;
            if (combat.modeSeconds >= duration) {
                combat.mode = PlayerMode::Grounded;
                combat.modeSeconds = 0.0f;
                combat.heavyHit = false;
            }
            break;
        }

        case PlayerMode::Dead:
            // Terminal until the R soft reset: no input, no gravity, no clip
            // choice of its own (the app plays and pins the death clip).
            break;
    }

    combat.grabWasDown = input.grab;
    combat.strikeWasDown = input.strike;
    return events;
}

bool player_take_damage(PlayerCombat& combat, float damage, bool heavy,
                        const CombatTuning& tuning, StepEvents* events) {
    if (combat.mode == PlayerMode::Dead || combat.invulnSeconds > 0.0f) {
        return false;  // dead men and active i-frames take no damage
    }
    if (!std::isfinite(damage) || damage <= 0.0f) {
        return false;
    }

    const float health = std::isfinite(combat.health) ? combat.health : 0.0f;
    const float maxHealth = std::isfinite(tuning.maxHealth) ? tuning.maxHealth : 0.0f;
    combat.health = std::clamp(health - damage, 0.0f, std::max(0.0f, maxHealth));
    combat.invulnSeconds =
        std::isfinite(tuning.invulnSeconds) ? std::max(0.0f, tuning.invulnSeconds) : 0.0f;

    if (combat.mode == PlayerMode::Falling) {
        // The fall owns the transition: lethal damage only arms the landing.
        // R7: a killing hit also closes the catch window, so a grab press
        // cannot escape the death the landing owes. The hit is still heard: the
        // air reaction is reported now and `died` arrives on landing.
        if (events != nullptr) {
            if (heavy) {
                events->hurtHeavy = true;
            } else {
                events->hurtLight = true;
            }
        }
        if (combat.health <= 0.0f) {
            combat.pendingDeath = true;
            combat.catchSecondsLeft = 0.0f;
        }
        return true;
    }

    if (combat.health <= 0.0f) {
        enter_dead(combat);
        if (events != nullptr) {
            events->died = true;
        }
        return true;
    }

    enter_hurt(combat, heavy);
    if (events != nullptr) {
        if (heavy) {
            events->hurtHeavy = true;
        } else {
            events->hurtLight = true;
        }
    }
    return true;
}

void reset_player_combat(PlayerState& player, PlayerCombat& combat, Vec3 spawnPosition,
                         float spawnYawDeg, const CombatTuning& tuning) {
    player.position = spawnPosition;
    player.yawDeg = spawnYawDeg;

    combat.mode = PlayerMode::Grounded;
    combat.modeSeconds = 0.0f;
    combat.stamina = kMaxStamina;
    combat.regenDelay = 0.0f;
    clear_fight_attachment(combat);
    combat.grabWasDown = false;
    combat.strikeWasDown = false;
    combat.health = std::isfinite(tuning.maxHealth) ? std::max(0.0f, tuning.maxHealth) : 0.0f;
    combat.invulnSeconds = 0.0f;
    combat.heavyHit = false;
    combat.pendingDeath = false;
}
