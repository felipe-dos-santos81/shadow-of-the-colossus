#pragma once

#include <span>

#include "behaviour.h"

// The encounter description (S11 Task 7, design D8 and rulings §13.10, A5): one
// constexpr table per colossus (`encounters/valus_desc.h`, later `quadratus.h`),
// selected by the arena manifest's `encounter <id>` directive (absent = valus).
// It holds what used to be literals scattered through grab/collision/valus/
// death/camera/audio_map/encounter_rt: the skeleton map, the props, the
// weakpoints, the climb bands, the collision capsules, the attacks, the death
// bundle, the cues, the target camera and the behaviour machine. Every link is
// an `int` index into the same table (-1 = none), so a table is a plain
// aggregate a test can static_assert. No I/O, no SDL; types only.

// --- Climb bands (grab.h's maths walks them) ----------------------------------

// An index into an encounter's band table. The names are Valus's bands, kept so
// code and tests can say `ClimbBandId::Head`; `None` (-1) is "no band" for every
// table, and `Count` is Valus's band count. It converts to and from `int`
// freely: a band link is a plain int.
struct ClimbBandId {
    enum Name : int {
        None = -1,
        LeftFoot,
        LeftShin,
        LeftThigh,
        Waist,
        Chest,
        Neck,
        Head,
        RightShoulder,
        RightArm,
        Count,
    };
    int index = None;
    constexpr ClimbBandId() = default;
    constexpr ClimbBandId(int i) : index(i) {}
    constexpr operator int() const { return index; }
};

// One capsule-shaped strip of the body between two posed skeleton joints.
// `up`/`down` are the bands above and below in the climb chain, `branch` the one
// lateral fork (W + a lateral input at the band top), `weakpoint` the index of
// the band's shell in the encounter's weakpoint table; each -1 for none.
// `name` is the band's stable status token (`left_foot` .. `right_arm`), which
// the frozen logs print.
struct ClimbBand {
    const char* boneA;  // segment start, a joint name in the colossus rig
    const char* boneB;  // segment end
    float radius;       // body radius around the segment (metres)
    int up;
    int down;
    int weakpoint;
    int branch = ClimbBandId::None;
    bool horizontal = false;  // S11 surface walk: stood on and walked, not hung from
    const char* name = "none";
    // S11 taper: the radius at boneB, linear from `radius` at boneA (< 0 = no
    // taper: `radius` throughout). grab.h's band_radius_at reads it.
    float radiusB = -1.0f;
    // S12 enable window (design D7): with `enableClip` set the band exists only
    // while the entity plays that clip at a frame in the half-open [enableFrom,
    // enableTo) — a frame time f is in it iff floor(f) is, so Gaius's blade
    // [9, 75) is the R3 decode's "frame time in [9.0, 75.0)" — and enableTo < 0
    // is open to the clip's end. Null (the default) = always there.
    const char* enableClip = nullptr;
    int enableFrom = 0;
    int enableTo = -1;
    // S12 walk pace on a walked band (m/s; 0 = CombatTuning::surfaceWalkSpeed).
    float walkSpeed = 0.0f;
    // S12 disable hand-over (§13.4): when the window closes with the player at
    // bandT >= 1 - carryFraction he is moved onto `carryTo` at its nearest
    // point; lower (or with no carryTo) he falls.
    float carryFraction = 0.0f;
    int carryTo = ClimbBandId::None;
    // S12 entry (D7): E grabs this band only within reach of its low end (the
    // lower posed end), never mid-band; the grab attaches there.
    bool grabEntryLowEnd = false;
};

// --- The rest of the description ----------------------------------------------

// The named joints the runtime reads besides the bands and capsules.
struct JointMap {
    const char* chest;  // the target camera's orbit point, the tendrils' centre
    const char* footL;  // the stomp selection's feet
    const char* footR;
};

enum class PropRole { Weapon, Corpse, Ground };

// A static model appended to the scene for the entity: the weapon rides the
// rig's `socketJoint`; the corpse and its ground piece sit at the body's
// placement (`socketJoint` null).
struct PropDesc {
    PropRole role;
    const char* glb;
    const char* socketJoint;
};

// A weakpoint shell: its material (the posed proxy) and the reaction a landed
// hit on it plays. What a hit does per state is the behaviour's gates (A5).
struct WeakpointDesc {
    const char* material;
    const char* reactionClip;
};

// One collision capsule between two joints (order = `Capsule::segment`).
struct CapsuleDesc {
    const char* from;
    const char* to;
    float radius;
};

// One attack. `reachMeters` is the selection range (Valus: a stomp's from the
// foot, the club's from the body); `facingDot` the front gate (dot > it; -1 =
// none); damage lands within `hitRangeMeters` of `damageJoint`, the lighter
// `waveDamage` within `waveRangeMeters` (0 = no wave); `swingSeconds` is the
// whoosh cue's clip time (-1 = none).
struct AttackDesc {
    const char* clip;
    const char* damageJoint;
    float reachMeters;
    float facingDot;
    float hitRangeMeters;
    float waveRangeMeters;
    float cooldownSeconds;
    float impactBegin;
    float impactEnd;
    float swingSeconds;
    float damage;
    float waveDamage;
    bool heavy;
};

// The death cinematic's keys (camera_death_pose): times on the death clock, then
// each key's distance/heights/angles relative to the entity.
struct DeathCameraDesc {
    float pullSeconds;
    float lowSeconds;
    float wideSeconds;
    float followSeconds;
    float pullDistance;
    float pullEyeHeight;
    float pullTargetHeight;
    float pullPitchDeg;
    float lowTargetHeight;
    float lowDistance;
    float lowPitchDeg;
    float lowYawOffsetDeg;
    float wideTargetHeight;
    float wideDistance;
    float widePitchDeg;
    float wideYawOffsetDeg;
};

struct DeathDesc {
    const char* clip;
    float motionSeconds;  // the death clip's runtime duration
    float tendrilStartSeconds;
    float tendrilEndSeconds;
    float endSeconds;  // the cinematic's end: control returns
    int roots;         // tendril roots
    float fallbackChestHeight;  // the tendrils' centre above the placement without a chest joint
    DeathCameraDesc camera;
};

// One audio cue: a manifest clip name (null = a deliberately silent event:
// `cue_for` answers nullptr, nothing is looked up or gated).
struct SoundCue {
    const char* clip;  // manifest name, never null for a mapped cue
    float gain;        // fractional voice gain (HurtHeavy is a 1.25 boost)
    bool loop;         // the clip's own loop flag is authoritative; informational
    bool s6Only = false;  // required only by the S6 slice's load gate (death, club swing)
};

// The encounter's own cues (audio_map's Sfx rows they fill) and its music.
struct CueDesc {
    SoundCue stompStart;
    SoundCue stompImpact;
    SoundCue clubStart;
    SoundCue clubImpact;
    SoundCue clubCancel;
    SoundCue reaction;
    SoundCue defeat;
    SoundCue footLeft;
    SoundCue footRight;
    SoundCue clubSwing;
    const char* music;  // null = a silent fight (no music, no load gate)
    float musicGain;
};

// The entity target-framing camera (T): orbit distance limits and defaults.
struct TargetCamDesc {
    float distanceMin;
    float distanceMax;
    float defaultDistance;
    float defaultPitchDeg;
    float pitchLimitDeg;
};

struct EncounterDesc {
    const char* id;
    float maxHealth;
    JointMap joints;
    std::span<const PropDesc> props;
    std::span<const WeakpointDesc> weakpoints;
    const char* bodyReactionClip;  // a landed hit off every weakpoint
    std::span<const ClimbBand> bands;
    std::span<const CapsuleDesc> capsules;
    float turnDegPerSec;
    std::span<const AttackDesc> attacks;
    DeathDesc death;
    CueDesc cues;
    TargetCamDesc targetCam;
    BehaviourDesc behaviour;
};

// The encounter's prop of `role`, or nullptr when it has none.
constexpr const PropDesc* encounter_prop(const EncounterDesc& desc, PropRole role) {
    for (const PropDesc& prop : desc.props) {
        if (prop.role == role) {
            return &prop;
        }
    }
    return nullptr;
}
